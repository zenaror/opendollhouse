#include "odh/routing.hpp"
#include "odh/route_frame.hpp"
#include "odh/slot_scoring.hpp"
#include "odh/source_direction.hpp"
#include "../tools/household_go_here_session.hpp"
#include "../tools/room_portal_session.hpp"
#include "../tools/route_outcome_log.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks=0;
void require(bool v,const char* message) {++checks;if(!v)throw std::runtime_error(message);}
// A native synthetic factory for controller tests. Legacy markers are formed
// explicitly AFTER new-request admission only to exercise old-save adoption.
bool request_go_here_fixture(odh_host::HouseholdGoHereSession& session,odh::World& world,odh::Vm& vm,
        const odh::EntityPosition& destination,odh::CodeOwner owner,std::optional<odh::RoutineSet> routines,
        std::uint16_t action=4096,bool legacy=false) {
    odh_host::HouseholdGoHereSession::Services services;
    services.source_still_matches=[](){return true;};
    services.construct=[&](const auto& point,const auto& current,std::string*)
            ->std::optional<odh_host::HouseholdGoHereSession::ConstructedDestination>{
        if(!current()||!owner||action!=4096||(!routines&&!vm.has_routine(odh::routine_key(action),owner)))return {};
        auto program=vm.shared_program_for_owner(owner);
        if(!program){program=std::make_shared<const odh::VmProgram>(std::move(*routines));
            if(!vm.install_shared_owner(owner,program))return {};}
        std::int16_t id=0;for(std::int32_t next=1;next<=32767;++next)
            if(world.entity_id_available(static_cast<std::int16_t>(next))){id=static_cast<std::int16_t>(next);break;}
        if(!id||!world.create_runtime_object(id,0x7c4,{},std::vector<odh::EntryPoint>(31))||
           !world.set_position(id,point))return {};
        odh::Interaction other{};other.action=4112;other.label_index=5;
        odh::Interaction go{};go.action=action;go.label_index=2;
        odh::Ttab table{odh::TtabStatus::decoded,2,9,true,0,{other,go}};
        return odh_host::HouseholdGoHereSession::ConstructedDestination{id,world.find(id)->generation,owner,program,table};
    };
    const auto accepted=session.request(destination,services);
    if(accepted&&legacy){const auto id=*session.marker_id();require(world.set_ui_ghost(id,true)&&world.set_static_footprint(id,false),
        "explicitly form a historical legacy marker fixture");}
    return accepted;
}
odh::SlotTable slot();
void busy_resident_go_here_admission(){
    odh::World world;require(world.create(7,odh::EntityKind::avatar,{0,88,3,16})&&world.create(9,odh::EntityKind::object)&&
        world.set_position(7,{4,4,1,0})&&world.set_position(9,{20,20,1,0})&&world.declare_object_word(7,8,16),
        "busy Go Here source resident");
    odh::EntityThreadControllers threads{world,7};odh::RouteGrid grid{64,64};odh::SlotTable globals;
    odh::Routing route{world,grid,globals};
    const odh::Bhav idle{0x8002,{{17,1,255,{0,0,1,0,0,0,0,0}},{2,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}};
    const odh::Bhav action{0x8002,{{2,254,255,{0,0,7,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,4,0}};
    odh::Vm vm{{{odh::routine_key(4096),idle}},world.vm_memory(),{},threads.interaction_primitive(),{{77,{{odh::routine_key(4100),action}}}}};
    require(threads.bind(vm)&&route.bind(vm)&&vm.start(odh::routine_key(4096),{7,7,7,0},{4})==odh::VmStatus::running&&
        threads.interactions().enqueue_source_ts1(odh::routine_key(4100),{7,9,9,77},std::vector<std::int16_t>(4),2,
            odh::InteractionQueueMode::normal,0,false,false)&&vm.run(1)==odh::VmStatus::budget_exhausted,
        "source autonomous action owns a genuine protected active queue prefix");
    const auto old=threads.interactions().snapshot().active->uid;
    odh_host::HouseholdGoHereSession session{world,7,vm,threads,route};
    const odh::RoutineSet destination{{odh::routine_key(4096),{0x8002,{{27,254,255,{10,0,254,255,10,0,5,0}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
    require(request_go_here_fixture(session,world,vm,{28,4,1,0},78,destination)&&threads.interactions().size()==2&&
        threads.interactions().snapshot().active->uid==old&&!threads.interactions().completion(),
        "ordinary Go Here construction admits busy source queue without erasing the protected action");
    require(threads.interactions().snapshot().actions.back().priority==50&&session.marker_alive(),
        "new Destination uses existing original TTAB source user priority and ordinary lifetime");
    odh::Vm foreign{{{odh::routine_key(4096),idle}},world.vm_memory()};
    require(foreign.start(odh::routine_key(4096),{7,7,7,0},{4})==odh::VmStatus::running&&foreign.run_generation()==vm.run_generation(),
        "different VM may legitimately share caller and run counter values");
    odh_host::HouseholdGoHereSession wrong_vm{world,7,foreign,threads,route};unsigned constructions=0;
    odh_host::HouseholdGoHereSession::Services unbound;unbound.source_still_matches=[](){return true;};
    unbound.construct=[&](const auto&,const auto&,std::string*)->std::optional<odh_host::HouseholdGoHereSession::ConstructedDestination>{++constructions;return {};};
    require(!wrong_vm.request({36,4,1,0},unbound)&&!constructions,
        "cross-VM Go Here binding rejects before ordinary construction or queue effects");
    odh_host::HouseholdGoHereSession changed{world,7,vm,threads,route};
    odh_host::HouseholdGoHereSession::Services services;services.source_still_matches=[](){return true;};
    services.construct=[&](const auto&,const auto& current,std::string*)->std::optional<odh_host::HouseholdGoHereSession::ConstructedDestination>{
        require(current(),"busy queue lease authentic before construction side effects");
        require(threads.interactions().cancel(old)==odh::InteractionCancelResult::notified&&!current(),
            "queue lease rejects changed source cancellation flags even when size and active UID are unchanged");return {};};
    require(!changed.request({36,4,1,0},services)&&threads.interactions().size()==2&&threads.interactions().snapshot().active->uid==old,
        "changed construction authority preserves legitimate existing queue effects without fabricated completion");
}
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
void reference_slot_point_tests() {
    odh::SlotRecord point{};point.type=3;point.offset_bits={0,0xC1800000U,0};
    point.standing=1;point.sitting=0;point.ground=0;point.flags=0;point.snap_target_slot=-1;
    point.proximity=std::array<std::int32_t,3>{16,16,16};point.max_size=0;point.unknown_i10=0;
    point.gradient_bits=0x3E400000;point.height=0;point.facing=4;point.resolution=16;
    const odh::SlotTable table{0,10,{point},0};
    const auto north=odh::enumerate_reference_slot_point_candidate(table,0,{100,100},0);
    require(north.position==odh::RoutePoint{100,84}&&north.facing==4,
            "House07 fixed SLOT offset preserves its authored point and facing");
    const auto east=odh::enumerate_reference_slot_point_candidate(table,0,{100,100},2);
    require(east.position==odh::RoutePoint{116,100}&&east.facing==6,
            "House07 fixed SLOT offset rotates one quarter turn deterministically");
    const auto diagonal=odh::enumerate_reference_slot_point_candidate(table,0,{100,100},1);
    require(diagonal.position==odh::RoutePoint{111,89}&&diagonal.facing==5,
            "House07 fixed SLOT offset rounds diagonal coordinates to the nearest even integer");
    bool rejected=false;auto malformed=table;malformed.records[0].offset_bits[1]=0xC1400000U;
    try {(void)odh::enumerate_reference_slot_point_candidate(malformed,0,{100,100},0);}
    catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"point-offset interpreter rejects offsets outside the four verified House07 values");
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
    odh::RouteGrid chair_grid(9,3);
    chair_grid.block({4,1});
    const std::vector<odh::PreRankedSlotGoal> chair_goals={
        {{4,1},1,2,2,odh::RoutePoint{3,1}}
    };
    result=odh::find_pre_ranked_slot_route(chair_grid,{0,1},chair_goals);
    require(result.path.status==odh::RouteStatus::found&&result.selected==0&&
            result.path.points.back()==odh::RoutePoint{3,1},
        "a seated SLOT can route to a clear adjacent point while preserving its occupied seat destination");
    auto malformed_chair=chair_goals;
    malformed_chair[0].sitting_object.reset();
    result=odh::find_pre_ranked_slot_route(chair_grid,{0,1},malformed_chair);
    require(result.path.status==odh::RouteStatus::invalid_input,
        "an approach point without a resolved chair identity is rejected");
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
    require(routing.can_release_idle_cache(),"unused controller has no execution state pinned to its cache");
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
    require(!routing.can_release_idle_cache(),"an admitted walk must retain its controller and grid");
    const auto active_vm=vm.snapshot();const auto active_actor=*world.find(1)->position;
    odh::RouteGrid peer(64,64);peer.block({28,20});
    require(odh::coalesce_route_grid_cache({&peer,&grid})==4096&&grid.same_contents(peer)&&
        !routing.can_release_idle_cache()&&routing.active()&&vm.pending()->ticket==active_vm.pending->ticket&&
        vm.steps()==active_vm.steps&&vm.stack().size()==active_vm.stack.size()&&
        world.find(1)->position==std::optional<odh::EntityPosition>{active_actor},
        "active borrowed Routing keeps exact pending VM and pose while only identical derived pages are shared");
    require(peer.block({5,20})&&grid.walkable({5,20}),"a peer cache mutation cannot modify the active route's planned path");
    for(std::uint64_t tick=1;tick<=32&&routing.active();++tick)routing.tick(tick);
    require(vm.status()==odh::VmStatus::returned_true&&!routing.active()&&
        world.find(1)->position->x==12&&world.find(1)->position->y==20&&
        world.find(1)->person_data[37]==0x40,
        "Door range Route45 reaches the ranked west approach while preserving entry flags");
    require(routing.can_release_idle_cache(),"completed source walk releases the controller's cache pin");

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
void position_publication_tests(){
    Fixture f;
    require(f.world.remove(1)&&f.world.create(1,odh::EntityKind::avatar,{0,88,1})&&f.world.set_position(1,{4,12,1,0})&&
        f.world.create(3,odh::EntityKind::object)&&f.world.declare_object_word(3,1,6)&&f.world.set_position(3,{4,12,1,6})&&
        f.world.attach(1,0,3),"actual route actor and carried child");
    bool pre=false,inflight_authority_valid=true;unsigned checks_run=0,publications=0,arrivals=0,inflight_publications=0,staging_callbacks=0;
    std::optional<odh::RoutePositionPublicationContext> retained;
    const odh::RoutePositionPublicationContext* inflight=nullptr;
    require(f.world.set_room_resolver([&](const auto&)->std::optional<std::uint16_t>{
        if(inflight){++staging_callbacks;require(inflight->source_still_matches(),"minted publication authority remains valid while scalar publisher stages old pose");}
        return 1;}),"bind actual publication staging observer");
    require(f.world.set_spatial_change_observer([&](odh::World& actual,std::uint64_t){
        if(inflight){++inflight_publications;const auto* actor=actual.find(1);
            inflight_authority_valid=inflight_authority_valid&&(&actual==&f.world)&&actor&&actor->position&&
                *actor->position==inflight->new_position&&inflight->source_still_matches();}
    }),"bind actual scalar publication spatial observer");
    f.routing.set_route_execution_callback(odh::RouteExecutionPhase::set_position,[&](const auto& context){
        ++checks_run;require(pre&&context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase),
            "existing PositionValid callback executes once after source pre-position event");
    });
    f.routing.set_position_publication_provider([&](const auto& context)->std::optional<odh::SourceSparsePlacementOutcome>{
        require(context.execution.vm==&f.vm&&context.execution.dispatch.origin==&f.vm&&f.vm.pending()&&
            context.execution.dispatch.ticket==f.vm.pending()->ticket&&context.execution.actor==1&&
            context.execution.actor_generation==f.world.find(1)->generation&&context.source_still_matches()&&
            context.execution.proof.matches(context.execution.frame,f.vm,context.execution.instruction,context.execution.dispatch,context.execution.phase),
            "publication carries actual pending VM/frame/ticket/generation capability");
        retained=context;const auto child_before=*f.world.find(3)->position;
        pre=true;odh::SourceSparsePlacementOutcome result;
        if(context.has_placement_check){const auto checked=context.check_position();require(checked.has_value(),"invoke existing source PositionValid once");result=*checked;}
        else{++arrivals;result.status=odh::SourceSparsePlacementStatus::success;}
        inflight=&context;
        require(context.source_still_matches()&&context.publish_position(),"source publication follows completed check");
        inflight=nullptr;
        require(*f.world.find(3)->position==child_before,"root publication does not prematurely move carried child");
        require(f.world.refresh_contained_position(3)&&f.world.find(3)->position->x==context.new_position.x&&
            f.world.find(3)->position->y==context.new_position.y&&f.world.find(3)->position->direction==6,
            "ordered child post-position refresh follows root publication and preserves child heading");
        require(context.source_still_matches(),"post-position events retain actual route capability");
        pre=false;++publications;return result;
    });
    f.start();require(f.routing.active()&&f.vm.status()==odh::VmStatus::waiting,"publication route starts through actual45");
    for(std::uint64_t tick=1;f.routing.active()&&tick<64;++tick){require(f.routing.tick(tick),"advance route through full position lifecycle");
        if(retained)require(!retained->source_still_matches()&&!retained->publish_position()&&!retained->check_position()&&
            !retained->execution.proof.matches(retained->execution.frame,f.vm,retained->execution.instruction,retained->execution.dispatch,retained->execution.phase),
            "retained publication/check/proof callbacks expire immediately after synchronous source event");}
    require(!f.routing.active()&&f.vm.status()==odh::VmStatus::returned_true&&checks_run>0&&arrivals==1&&publications==checks_run+arrivals&&
        inflight_authority_valid&&inflight_publications==publications&&staging_callbacks>=publications,
        "ordinary route steps and final arrival use same native position lifecycle without duplicated checks");

    Fixture skipped;skipped.routing.set_position_publication_provider([](const auto&)->std::optional<odh::SourceSparsePlacementOutcome>{
        odh::SourceSparsePlacementOutcome result;result.status=odh::SourceSparsePlacementStatus::success;return result;});
    skipped.start();const auto before=*skipped.world.find(1)->position;
    require(!skipped.routing.tick(1)&&*skipped.world.find(1)->position==before&&skipped.vm.status()==odh::VmStatus::waiting,
        "callback success without physical publication is not source movement");

    Fixture forged;forged.routing.set_position_publication_provider([&](const auto& context)->std::optional<odh::SourceSparsePlacementOutcome>{
        require(forged.world.place_individual_on_floor(1,context.new_position,context.new_position.direction)&&!context.source_still_matches()&&!context.publish_position(),
            "external new pose does not gain in-flight authority without entering minted scalar publisher");
        odh::SourceSparsePlacementOutcome result;result.status=odh::SourceSparsePlacementStatus::success;return result;});
    forged.start();require(!forged.routing.tick(1),"unowned physical mutation remains unsupported even at the intended destination");

    Fixture twice;twice.routing.set_position_publication_provider([&](const auto& context)->std::optional<odh::SourceSparsePlacementOutcome>{
        require(context.publish_position()&&!context.publish_position(),"same source lease cannot publish twice");
        odh::SourceSparsePlacementOutcome result;result.status=odh::SourceSparsePlacementStatus::success;return result;});
    twice.start();const auto old=*twice.world.find(1)->position;
    require(!twice.routing.tick(1)&&!(*twice.world.find(1)->position==old),"invalid repeated publication retains actual first effects without rollback");

    Fixture bypass;unsigned bypass_checks=0;
    bypass.routing.set_route_execution_callback(odh::RouteExecutionPhase::set_position,[&](const auto&){++bypass_checks;});
    bypass.routing.set_position_publication_provider([](const auto& context)->std::optional<odh::SourceSparsePlacementOutcome>{
        require(context.has_placement_check&&!context.publish_position(),"existing source PositionValid cannot be skipped");
        odh::SourceSparsePlacementOutcome result;result.status=odh::SourceSparsePlacementStatus::success;return result;});
    bypass.start();require(!bypass.routing.tick(1)&&bypass_checks==0,"bypassed source check cannot produce movement or completion");

    Fixture blocked;blocked.routing.set_position_publication_provider([](const auto& context)->std::optional<odh::SourceSparsePlacementOutcome>{
        require(context.source_still_matches(),"blocked source event retains actual old-pose capability");
        odh::SourceSparsePlacementOutcome result;result.status=odh::SourceSparsePlacementStatus::blocked_object;return result;});
    blocked.start();const auto blocked_pose=*blocked.world.find(1)->position;
    require(blocked.routing.tick(1)&&!blocked.routing.active()&&blocked.vm.status()==odh::VmStatus::returned_false&&*blocked.world.find(1)->position==blocked_pose,
        "authored blocked placement returns route false and preserves old physical pose");
}
struct CarriedPublicationFixture {
    odh::World world;odh::RouteGrid grid{64,64};
    static odh::SlotTable slots(){odh::SlotRecord row{};row.type=3;row.standing=1;row.snap_target_slot=-1;
        row.proximity=std::array<std::int32_t,3>{16,16,16};row.gradient_bits=0x3e400000;
        row.offset_bits[0]=0x3f800000;row.facing=-2;row.resolution=16;
        return {0,10,std::vector<odh::SlotRecord>(9,row),0};}
    odh::SlotTable table=slots();odh::Routing routing{world,grid,table};
    odh::RoutineSet code{{odh::routine_key(4096),{0x8002,{{45,254,255,{8,0,1,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
    odh::Vm vm{code,world.vm_memory(),{},routing.primitive()};
    std::int16_t target=1;unsigned depth=0,checks_seen=0,publications=0;bool pre=false;
    std::optional<odh::RoutePositionPublicationContext> retained;
    std::function<void(const odh::RoutePositionPublicationContext&)> mutation;
    std::optional<odh::SourceSparsePlacementOutcome> forced_placement;
    explicit CarriedPublicationFixture(unsigned levels,const odh::WorldSnapshot* saved=nullptr,bool relative=false)
        :code(relative?goto_relative():odh::RoutineSet{{odh::routine_key(4096),{0x8002,{{45,254,255,{8,0,1,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,4,4,0}}}}),
         vm(code,world.vm_memory(),{},routing.primitive()),depth(levels){
        target=depth==0?1:depth==1?2:3;
        if(saved)require(world.restore_snapshot(*saved),"fresh carried world restores owned state");
        else{
            require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&world.set_position(1,{32,32,1,0}),"source carried actor");
            if(depth)require(world.create(2,odh::EntityKind::object,{0,0,1})&&
                world.declare_object_word(2,1,6)&&world.declare_object_word(2,23,0)&&
                world.set_position(2,{32,32,1,6})&&world.attach(1,0,2),
                "source carried child has explicit Direction and unrestricted RotationNotches before real heading writes");
            if(depth==2)require(world.create(3,odh::EntityKind::avatar,{0,88,0})&&world.set_position(3,{32,32,1,3})&&
                world.set_radian_direction(3,2.4f)&&world.attach(2,0,3),"source nested target retains continuous angle");
            for(std::int16_t id=1;id<=target;++id)require(world.declare_object_word(id,8,0),"explicit source collision flags");
        }
        const auto generation=world.find(1)->generation;
        routing.set_stack_object_slot_table_provider([this](const auto&){return &table;});
        routing.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
        routing.set_route_goal_validator([](const auto&,auto){return true;});
        odh::SourceSparseServices services;
        services.capture_avatars_to_consider=[](const auto& context,const auto& inherited)->std::optional<std::vector<odh::LiveAttemptWalkIdentity>>{
            if(!context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))return {};
            return inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});};
        services.plan_input=[this,generation](const auto& context,odh::RoutePoint goal,std::optional<float> outgoing,auto inherited)->std::optional<odh::SourceSparsePlanInput>{
            odh::SourceSparsePlanInput input;input.profile={false,88,false,0};input.avatars_to_consider=inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});
            input.request.actor={1,generation};input.request.destination={goal.x,goal.y};input.request.bezier=false;input.request.outgoing_direction=outgoing;
            input.services.context_still_matches=[context]{return context.proof.matches_after_restore(context.frame,*context.vm,context.instruction,context.dispatch,context.phase);};
            input.services.room_at=[](const auto&){return std::optional<std::uint16_t>{1};};
            input.services.domain=[this,generation](auto){odh::SourceWalkDomain domain;domain.state=odh::LiveAttemptWalkState::ready;
                domain.request.actor={1,generation};domain.request.room=1;domain.request.level=1;domain.request.room_bounds={0,0,64,64};
                domain.request.architecture_complete=true;domain.request.entities_complete=true;domain.request.avatars_to_consider_complete=true;
                for(const auto& e:world.snapshot().entities)domain.request.entities.push_back({e.id,e.generation});
                domain.request.architectural_and_bounds={{-32,-32,1056,-16},{-32,1040,1056,1056},{-32,-32,-16,1056},{1040,-32,1056,1056}};
                domain.services.inventory_still_matches=[](const auto&){return true;};
                domain.services.footprint=[](const auto&){return odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::absent,{}};};
                domain.services.entry_point5=[](const auto&){return odh::LiveAttemptWalkEntryPoint5Result{};};return domain;};return input;};
        services.place=[this](const auto& context,const auto&)->std::optional<odh::SourceSparsePlacementOutcome>{
            require(pre&&context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase),"carried sparse check follows Pre exactly once");
            ++checks_seen;if(forced_placement)return forced_placement;
            odh::SourceSparsePlacementOutcome result;result.status=odh::SourceSparsePlacementStatus::success;return result;};
        services.source_tick=[](const auto&){return true;};services.publish_velocity=[](const auto&,const auto&,auto*){return true;};
        services.locomotion_profile=[](const auto&)->std::optional<odh::WalkMotionProfile>{return odh::WalkMotionProfile{false,88,false,0};};
        services.begin_locomotion=[](const auto&,const auto&,auto*){return true;};services.end_locomotion=services.begin_locomotion;
        services.publish_motion=[](const auto&,const auto&,const auto&,auto*){return true;};routing.set_source_sparse_services(std::move(services));
        routing.set_look_towards_services({[](const auto&)->std::optional<odh::SourceLookTowardsProfile>{return odh::SourceLookTowardsProfile{0,false};},
            [](const auto&,auto,auto,auto,auto)->std::optional<std::uint32_t>{return 1;},[](const auto&,auto)->std::optional<bool>{return true;},[](const auto&,auto,bool){return true;}});
        routing.set_position_publication_provider([this](const auto& context)->std::optional<odh::SourceSparsePlacementOutcome>{
            retained=context;require(context.source_still_matches(),"actual carried source lease starts valid");
            pre=true;const auto checked=context.check_position();require(checked.has_value(),"source carried PositionValid runs once");
            if(checked->status!=odh::SourceSparsePlacementStatus::success){pre=false;return checked;}
            require(context.publish_position()&&context.source_still_matches(),"fixed scalar root preserves old carried target before Post");
            if(mutation)mutation(context);
            else{
                if(depth)require(world.refresh_contained_position(2)&&context.source_still_matches(),"direct child target refresh retains exact lease");
                if(depth==2)require(world.refresh_contained_position(3)&&context.source_still_matches()&&world.find(3)->radian_direction==2.4f,"nested target refresh preserves angle and exact lease");
            }
            ++publications;pre=false;return checked;
        });
        require(routing.bind(vm),"carried source VM binds actual routing");
    }
    void start(){
        const auto started=vm.start(odh::routine_key(4096),{1,1,target,0});
        require(started==odh::VmStatus::running,"start carried source VM");
        const auto status=vm.run(10);
        require(status==odh::VmStatus::waiting&&routing.active(),("actual carried-target Route45 starts: depth="+std::to_string(depth)+
            " status="+std::to_string(static_cast<int>(status))+" vm="+vm.diagnostic()+" routing="+routing.diagnostic()).c_str());
    }
};
void moving_source_target_tests(){
    const auto publisher=[](CarriedPublicationFixture& f){
        f.routing.set_position_publication_provider([&f](const auto& context)->std::optional<odh::SourceSparsePlacementOutcome>{
            require(context.source_still_matches(),"moving exact target is captured at this actual publication");
            f.pre=true;const auto checked=context.check_position();f.pre=false;
            if(!checked||checked->status!=odh::SourceSparsePlacementStatus::success)return checked;
            require(context.publish_position()&&context.source_still_matches(),"target remains pinned during actual scalar publication");
            ++f.publications;return checked;
        });
    };
    for(const bool relative:{false,true}){
        CarriedPublicationFixture f(1,nullptr,relative);
        require(f.world.detach(1,0)&&f.world.set_position(2,{64,48,1,6}),"independent source target starts at an authored anchor");
        publisher(f);f.start();
        const auto anchor=*f.world.find(2)->position;
        const auto point=odh::enumerate_source_slot_point_candidate(f.table,8,anchor,odh::source_game_object_radians(anchor.direction));
        const odh::RoutePoint goal=relative?odh::RoutePoint{anchor.x,anchor.y}:odh::RoutePoint{point.position.x,point.position.y};
        require(f.world.set_position(2,{80,64,1,2}),"same exact target moves and rotates between routing ticks");
        std::uint64_t tick=1;
        for(;tick<128&&f.publications<2;++tick)require(f.routing.tick(tick),("moving source tick: "+f.routing.diagnostic()).c_str());
        std::string reason;const auto local=f.routing.capture_local_session_route(odh::CodeOwner{0},&reason);
        const auto stack=f.routing.capture_session_route_stack(odh::CodeOwner{0},&reason);
        require(local&&stack,("moving source route captures authored goal and current target separately: "+reason).c_str());
        require(odh::route_has_captured_source_goals(*local)&&local->target_position==anchor&&local->path.back()==goal,"moving target does not rewrite authored anchor or selected goal");
        const auto world=f.world.snapshot();const auto vm=f.vm.snapshot();
        CarriedPublicationFixture fresh(1,&world,relative),stacked(1,&world,relative);publisher(fresh);publisher(stacked);
        require(fresh.vm.restore_snapshot(vm)&&fresh.routing.restore_local_session_route(*local,odh::CodeOwner{0})&&
            stacked.vm.restore_snapshot(vm)&&stacked.routing.restore_session_route_stack(*stack,odh::CodeOwner{0},&reason),
            ("fresh local/full-stack source routes accept moving exact target: "+reason).c_str());
        CarriedPublicationFixture wrong(1,&world,relative);publisher(wrong);
        auto stale=*local;++stale.target_generation;
        require(wrong.vm.restore_snapshot(vm)&&!wrong.routing.restore_local_session_route(stale,odh::CodeOwner{0}),"moving target allowance never admits wrong generation");
        CarriedPublicationFixture reused(1,&world,relative);publisher(reused);
        require(reused.vm.restore_snapshot(vm)&&reused.routing.restore_local_session_route(*local,odh::CodeOwner{0})&&
            reused.world.remove(2)&&reused.world.create(2,odh::EntityKind::object)&&reused.world.set_position(2,{80,64,1,2}),
            "routing participant is removed and its numeric ID legitimately reused");
        reused.routing.tick(tick);
        require(!reused.routing.active()&&reused.vm.status()==odh::VmStatus::returned_false,"removed/reused target does not alias the captured body or gain source authority");
        for(;tick<512&&(f.routing.active()||fresh.routing.active()||stacked.routing.active());++tick){
            if(f.routing.active())require(f.routing.tick(tick),("moving source completes: "+f.routing.diagnostic()).c_str());
            if(fresh.routing.active())require(fresh.routing.tick(tick),("restored moving source completes: "+fresh.routing.diagnostic()).c_str());
            if(stacked.routing.active())require(stacked.routing.tick(tick),("stack restored moving source completes: "+stacked.routing.diagnostic()).c_str());
        }
        require(!f.routing.active()&&!fresh.routing.active()&&!stacked.routing.active()&&
            f.vm.status()==odh::VmStatus::returned_true&&fresh.vm.status()==odh::VmStatus::returned_true&&stacked.vm.status()==odh::VmStatus::returned_true&&
            f.world.find(1)->position==fresh.world.find(1)->position&&f.world.find(1)->position==stacked.world.find(1)->position&&
            f.world.find(1)->position->x==goal.x&&f.world.find(1)->position->y==goal.y&&
            f.world.find(2)->position==fresh.world.find(2)->position&&f.checks_seen==f.publications,
            "captured source goals complete identically live and after fresh restore without following target movement");
    }
    Fixture legacy;legacy.start();require(legacy.world.set_position(2,{36,12,1,0}),"legacy target moves during route");
    legacy.routing.tick(1);
    require(!legacy.routing.active()&&legacy.vm.status()==odh::VmStatus::returned_false,"synthetic legacy target pose guard is preserved");
}
void carried_target_publication_tests(){
    for(unsigned depth=0;depth<3;++depth){
        std::vector<odh::RouteOutcomeReceipt> outcomes,fresh_outcomes,stack_outcomes;
        CarriedPublicationFixture f(depth);f.routing.set_outcome_observer([&](const auto& value){outcomes.push_back(value);});f.start();
        std::uint64_t tick=1;
        for(;f.routing.active()&&!f.publications&&tick<100;++tick)require(f.routing.tick(tick),("carried first movement: "+f.routing.diagnostic()).c_str());
        require(f.publications&&f.retained&&!f.retained->source_still_matches(),"source publication expires after full held-target Post");
        std::string reason;const auto mid=f.routing.capture_local_session_route(odh::CodeOwner{0},&reason);
        require(mid.has_value(),("carried midwalk captures original anchor and live ancestry: "+reason).c_str());
        require(mid->carried_target.size()==depth&&(depth==0||mid->target_position==mid->carried_target.front().original),"frozen geometry remains separate from live carried target");
        const auto world=f.world.snapshot();const auto vm=f.vm.snapshot();
        const auto stack=f.routing.capture_session_route_stack(odh::CodeOwner{0},&reason);
        require(stack&&stack->jobs.size()==1&&stack->jobs.front().carried_target.size()==depth,
            ("full route stack captures original carried ancestry: "+reason).c_str());
        CarriedPublicationFixture fresh(depth,&world);
        fresh.routing.set_outcome_observer([&](const auto& value){fresh_outcomes.push_back(value);});
        require(fresh.vm.restore_snapshot(vm),"fresh VM restores actual held-target pending frame");
        require(fresh.routing.restore_local_session_route(*mid,odh::CodeOwner{0}),"fresh held-target local route restores exact resource geometry");
        CarriedPublicationFixture stacked(depth,&world);
        stacked.routing.set_outcome_observer([&](const auto& value){stack_outcomes.push_back(value);});
        require(stacked.vm.restore_snapshot(vm)&&stacked.routing.restore_session_route_stack(*stack,odh::CodeOwner{0},&reason),
            ("fresh held-target full route stack restores exact ancestry: "+reason).c_str());
        if(depth==1){
            CarriedPublicationFixture doomed(depth,&world);
            require(doomed.vm.restore_snapshot(vm)&&doomed.routing.restore_local_session_route(*mid,odh::CodeOwner{0}),"restore pending carried route before deletion");
            const auto old=doomed.world.find(doomed.target)->generation;
            require(doomed.world.remove_retaining_body({doomed.target,old})&&doomed.world.create(doomed.target,odh::EntityKind::object)&&
                doomed.world.set_position(doomed.target,{48,48,1,2}),"dead carried target detaches and numeric ID is reused");
            const auto dead_local=doomed.routing.capture_local_session_route(odh::CodeOwner{0},&reason);
            const auto dead_stack=doomed.routing.capture_session_route_stack(odh::CodeOwner{0},&reason);
            require(dead_local&&dead_stack&&dead_local->target_generation==old,"pending dead target is captured by body reference without borrowing replacement");
            const auto dead_world=doomed.world.snapshot();const auto dead_vm=doomed.vm.snapshot();
            CarriedPublicationFixture dead_fresh(depth,&dead_world),dead_stacked(depth,&dead_world);
            require(dead_fresh.vm.restore_snapshot(dead_vm)&&dead_fresh.routing.restore_local_session_route(*dead_local,odh::CodeOwner{0})&&
                dead_stacked.vm.restore_snapshot(dead_vm)&&dead_stacked.routing.restore_session_route_stack(*dead_stack,odh::CodeOwner{0},&reason),
                ("fresh pending dead target restores before any source dispatch: "+reason).c_str());
            require(dead_fresh.world.find(doomed.target)->generation!=old&&dead_fresh.world.frame_entity(dead_fresh.vm.stack().back(),odh::VmEntityRole::stack_object)->generation==old&&
                dead_fresh.world.find(1)->position==doomed.world.find(1)->position,"loading retains old dead routing target and does not move actor");
        }
        auto wrong=*mid;if(depth){++wrong.carried_target.front().parent.generation;
            CarriedPublicationFixture rejected(depth,&world);require(rejected.vm.restore_snapshot(vm)&&
                !rejected.routing.restore_local_session_route(wrong,odh::CodeOwner{0}),"fresh restore rejects mismatched carried parent generation");}
        for(;tick<256&&(f.routing.active()||fresh.routing.active()||stacked.routing.active());++tick){
            if(f.routing.active())require(f.routing.tick(tick),("full carried route: "+f.routing.diagnostic()).c_str());
            if(fresh.routing.active())require(fresh.routing.tick(tick),("fresh full carried route: "+fresh.routing.diagnostic()).c_str());
            if(stacked.routing.active())require(stacked.routing.tick(tick),("fresh full carried stack: "+stacked.routing.diagnostic()).c_str());
        }
        if(f.vm.status()==odh::VmStatus::running)f.vm.run(10);
        if(fresh.vm.status()==odh::VmStatus::running)fresh.vm.run(10);
        if(stacked.vm.status()==odh::VmStatus::running)stacked.vm.run(10);
        require(!f.routing.active()&&!fresh.routing.active()&&!stacked.routing.active()&&f.vm.status()==odh::VmStatus::returned_true&&fresh.vm.status()==odh::VmStatus::returned_true&&stacked.vm.status()==odh::VmStatus::returned_true&&
            f.world.find(1)->position==fresh.world.find(1)->position&&f.world.find(f.target)->position==fresh.world.find(f.target)->position&&
            f.world.find(f.target)->position==stacked.world.find(stacked.target)->position&&f.checks_seen==f.publications&&fresh.checks_seen==fresh.publications&&stacked.checks_seen==stacked.publications,
            "actor/child/nested source walks complete identically after fresh midwalk local/stack restore and exactly-once checks");
        for(const auto* rows:{&outcomes,&fresh_outcomes,&stack_outcomes})require(rows->size()==1&&rows->front().stage==odh::RouteOutcomeStage::completion&&
            rows->front().result&&rows->front().completion_attempted&&rows->front().completion_accepted&&rows->front().source_available&&
            rows->front().source_is_pending&&rows->front().routine.id==4096&&rows->front().ticket==mid->ticket&&
            rows->front().actor.generation==f.world.find(1)->generation&&rows->front().target.generation==f.world.find(f.target)->generation,
            "terminal receipt covers actual original pending ticket after uninterrupted/local/full-stack restored walks");
        require(!outcomes.front().restored_job&&fresh_outcomes.front().restored_job&&stack_outcomes.front().restored_job,"restored routes explicitly lack earlier diagnostic history");

    }
    for(unsigned invalid=0;invalid<4;++invalid){
        CarriedPublicationFixture f(1);
        if(invalid==3)require(f.world.detach(1,0),"unrelated source target is outside the captured actor subtree");
        f.mutation=[&](const auto& context){
            if(invalid==0)require(f.world.detach(1,0)&&f.world.create(3,odh::EntityKind::object,{0,0,1})&&f.world.set_position(3,context.new_position)&&
                f.world.attach(3,0,2)&&f.world.refresh_contained_position(2),"actual target reparent mutation");
            else if(invalid==1){odh::VmFrame frame;frame.context.caller=2;require(f.world.refresh_contained_position(2)&&f.world.write(frame,3,1,7),"actual target heading mutation");}
            else if(invalid==2)require(f.world.remove(2)&&f.world.create(2,odh::EntityKind::object)&&f.world.set_position(2,context.new_position)&&
                f.world.attach(1,0,2)&&f.world.refresh_contained_position(2),"actual target generation mutation");
            else require(f.world.place_individual_on_floor(2,context.new_position,6),"unrelated target is physically moved to predicted actor XYZ");
            require(!context.source_still_matches(),"reparent/ownheading/reuse cannot inherit carried source authority");
        };
        f.start();bool failed=false;
        for(std::uint64_t tick=1;f.routing.active()&&tick<100;++tick)if(!f.routing.tick(tick)){failed=true;break;}
        require(failed&&f.publications==1,("invalid target mutation rejects source publication with real effects retained: mutation="+
            std::to_string(invalid)+" tick_failed="+std::to_string(failed)+" publications="+std::to_string(f.publications)+
            " route_active="+std::to_string(f.routing.active())+" vm_status="+std::to_string(static_cast<int>(f.vm.status()))+
            " route="+f.routing.diagnostic()+" vm="+f.vm.diagnostic()).c_str());
    }
}
void sparse_position_publication_tests(){
  for(const bool inherited_dynamic:{false,true})for(const bool reopen:{false,true}) {
    odh::World world;odh::RouteGrid grid{64,64};odh::SlotRecord record{};record.type=3;record.standing=1;record.sitting=0;record.ground=0;
    record.snap_target_slot=-1;record.proximity=std::array<std::int32_t,3>{16,16,16};record.gradient_bits=0x3e400000;record.facing=-2;record.resolution=16;
    odh::SlotTable table{0,10,std::vector<odh::SlotRecord>(9,record),0};odh::Routing routing{world,grid,table};
    require(world.create(1,odh::EntityKind::avatar,{0,88,0})&&world.create(2,odh::EntityKind::object)&&
        world.set_position(1,{4,12,1,0})&&world.set_position(2,{20,20,1,0}),"source sparse publication participants");
    require(world.set_static_footprint(1,!inherited_dynamic),"sparse route inherits prior static/dynamic obstacle class");
    require(world.declare_object_word(1,8,0)&&world.declare_object_word(2,8,0),"source sparse publication declares real collision flags");
    const auto actor_generation=world.find(1)->generation,target_generation=world.find(2)->generation;
    routing.set_stack_object_slot_table_provider([&](const auto&){return &table;});
    routing.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
    routing.set_route_goal_validator([](const auto&,auto){return true;});
    bool pre=false;unsigned placement_checks=0,publications=0,cant_wall=0;std::optional<odh::EntityPosition> failed_pose;
    odh::SourceSparseServices services;
    services.capture_avatars_to_consider=[](const auto& context,const auto& inherited)->std::optional<std::vector<odh::LiveAttemptWalkIdentity>>{
        if(!context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))return {};return inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});};
    services.plan_input=[&](const auto& context,odh::RoutePoint goal,std::optional<float> outgoing,auto inherited)->std::optional<odh::SourceSparsePlanInput>{
        odh::SourceSparsePlanInput input;input.profile={false,88,false,0};input.avatars_to_consider=inherited.value_or(std::vector<odh::LiveAttemptWalkIdentity>{});
        input.request.actor={1,actor_generation};input.request.destination={goal.x,goal.y};input.request.bezier=false;input.request.outgoing_direction=outgoing;
        input.services.context_still_matches=[context]{return context.proof.matches_after_restore(context.frame,*context.vm,context.instruction,context.dispatch,context.phase);};
        input.services.room_at=[](const auto&){return std::optional<std::uint16_t>{1};};
        input.services.domain=[&](auto){odh::SourceWalkDomain domain;domain.state=odh::LiveAttemptWalkState::ready;
            domain.request.actor={1,actor_generation};domain.request.room=1;domain.request.level=1;domain.request.room_bounds={0,0,64,64};
            domain.request.architecture_complete=true;domain.request.entities_complete=true;domain.request.avatars_to_consider_complete=true;
            domain.request.entities={{1,actor_generation},{2,target_generation}};
            domain.request.architectural_and_bounds={{-32,-32,1056,-16},{-32,1040,1056,1056},{-32,-32,-16,1056},{1040,-32,1056,1056}};
            domain.services.inventory_still_matches=[](const auto&){return true;};
            domain.services.footprint=[](const auto&){return odh::LiveAttemptWalkFootprint{odh::LiveAttemptWalkFootprintState::absent,{}};};
            domain.services.entry_point5=[](const auto&){return odh::LiveAttemptWalkEntryPoint5Result{};};return domain;};return input;};
    services.place=[&](const auto& context,const auto&)->std::optional<odh::SourceSparsePlacementOutcome>{
        require(pre&&context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch,context.phase),"source sparse PositionValid moved after actual pre-position event");
        ++placement_checks;odh::SourceSparsePlacementOutcome result;result.status=placement_checks==1?odh::SourceSparsePlacementStatus::cant_be_through_wall:odh::SourceSparsePlacementStatus::success;return result;};
    services.source_tick=[](const auto&){return true;};services.publish_velocity=[](const auto&,const auto&,auto*){return true;};
    services.locomotion_profile=[](const auto&)->std::optional<odh::WalkMotionProfile>{return odh::WalkMotionProfile{false,88,false,0};};
    services.begin_locomotion=[](const auto&,const auto&,auto*){return true;};services.end_locomotion=services.begin_locomotion;
    services.publish_motion=[](const auto&,const auto&,const auto&,auto*){return true;};routing.set_source_sparse_services(services);
    bool turn_ended=false;
    odh::SourceLookTowardsServices turns{[](const auto&)->std::optional<odh::SourceLookTowardsProfile>{return odh::SourceLookTowardsProfile{0,false};},
        [](const auto&,auto,auto,auto,auto)->std::optional<std::uint32_t>{return 20;},[&](const auto&,auto)->std::optional<bool>{return turn_ended;},[](const auto&,auto,bool){return true;}};
    routing.set_look_towards_services(turns);
    const auto publish=[&](const odh::RoutePositionPublicationContext& context)->std::optional<odh::SourceSparsePlacementOutcome>{
        require(context.has_placement_check&&context.source_still_matches(),"source sparse publication carries actual check capability");
        pre=true;const auto result=context.check_position();require(result.has_value(),"exactly one existing source sparse check");
        if(result->status==odh::SourceSparsePlacementStatus::success){require(context.publish_position(),"source accepted step physically publishes once");++publications;}
        else{++cant_wall;failed_pose=context.old_position;require(*world.find(1)->position==context.old_position,"CantBeThroughWall restores old physical pose before source post-position");}
        pre=false;return result;};
    routing.set_position_publication_provider(publish);
    odh::RoutineSet code{{odh::routine_key(4096),{0x8002,{{45,254,255,{8,0,1,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,4,4,0}}}};
    odh::Vm vm{code,world.vm_memory(),{},routing.primitive()};require(routing.bind(vm),"source sparse publication binds actual VM");
    require(vm.start(odh::routine_key(4096),{1,1,2,0})==odh::VmStatus::running,"source sparse publication starts actual VM");
    const auto started_status=vm.run(8);
    require(started_status==odh::VmStatus::waiting,("actual generic source SLOT route starts: status="+std::to_string(static_cast<int>(started_status))+", route="+routing.diagnostic()+", vm="+vm.diagnostic()).c_str());
    require(routing.tick(1),"advance inherited BeginTurn without starting its own locomotion");
    std::string why;const auto saved=routing.capture_session_route_stack(0,&why);
    require(saved&&saved->jobs.back().frame.state==odh::RouteFrameState::begin_turn&&
        saved->jobs.back().source_sparse&&saved->jobs.back().source_sparse->lifecycle==1&&
        !saved->jobs.back().source_sparse->locomotion_started&&
        saved->jobs.back().walking_started==inherited_dynamic&&world.find(1)->static_footprint==!inherited_dynamic,
        ("BeginTurn preserves inherited footprint and has saveable exact baseline: "+why).c_str());
    require(world.set_static_footprint(1,inherited_dynamic),"change footprint externally after admitted turn");
    require(!routing.capture_session_route_stack(0,&why)&&why.find("active route actor moved")!=std::string::npos,
        "footprint baseline still rejects later external changes during BeginTurn");
    require(world.set_static_footprint(1,!inherited_dynamic),"restore actual footprint for independent continuation");
    odh::Routing resumed{world,grid,table};
    resumed.set_stack_object_slot_table_provider([&](const auto&){return &table;});
    resumed.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
    resumed.set_route_goal_validator([](const auto&,auto){return true;});
    resumed.set_source_sparse_services(services);resumed.set_look_towards_services(turns);
    resumed.set_position_publication_provider(publish);
    odh::Vm resumed_vm{code,world.vm_memory(),{},resumed.primitive()};
    auto* active_route=&routing;auto* active_vm=&vm;
    if(reopen) {
        require(resumed_vm.restore_snapshot(vm.snapshot())&&resumed.bind(resumed_vm),"restore exact sparse VM before its route");
        auto changed=*saved;changed.jobs.back().walking_started=!changed.jobs.back().walking_started;
        require(!resumed.restore_session_route_stack(changed,0,&why)&&!resumed.active(),
            "restoring BeginTurn cannot replace the captured footprint baseline");
        require(resumed.restore_session_route_stack(*saved,0,&why),
            ("restore BeginTurn with inherited footprint without rewriting World: "+why).c_str());
        active_route=&resumed;active_vm=&resumed_vm;
    }
    turn_ended=true;
    for(std::uint64_t tick=2;active_route->active()&&tick<200;++tick){require(active_route->tick(tick),"advance source sparse position lifecycle");
        if(failed_pose&&placement_checks==1)require(world.find(1)->position->x==failed_pose->x&&world.find(1)->position->y==failed_pose->y,
            "source CantWall advances motion without inventing a successful logical placement");}
    if(active_vm->status()==odh::VmStatus::running)active_vm->run(8);
    require(!active_route->active()&&active_vm->status()==odh::VmStatus::returned_true&&world.find(1)->static_footprint&&
        cant_wall==1&&publications>0&&placement_checks==publications+cant_wall,
        "generic sparse path completes through exactly-once checks and preserves CantWall physical failure semantics");
  }
}

void outcome_receipt_tests(){
    const odh::PreRankedSlotGoalProvider goals=[](const auto&,std::size_t,const auto&,const auto&){return std::vector<odh::PreRankedSlotGoal>{{{44,12},4,7}};};
    Fixture observed(goals),control(goals);
    observed.routing.set_outcome_observer([](const auto&){throw std::runtime_error("diagnostic sink failure");});
    observed.start();control.start();
    for(std::uint64_t tick=1;tick<64&&(observed.routing.active()||control.routing.active());++tick){
        if(observed.routing.active())require(observed.routing.tick(tick),"observed route ticks despite diagnostic exception");
        if(control.routing.active())require(control.routing.tick(tick),"unobserved control ticks");
    }
    require(!observed.routing.active()&&!control.routing.active()&&observed.vm.status()==control.vm.status()&&
        observed.vm.steps()==control.vm.steps()&&observed.world.find(1)->position==control.world.find(1)->position,
        "throwing terminal observer does not change branch, VM steps or position");
    CarriedPublicationFixture blocked(0),blocked_control(0);std::vector<odh::RouteOutcomeReceipt> blocked_outcomes;
    for(auto* fixture:{&blocked,&blocked_control}){
        require(fixture->world.create(2,odh::EntityKind::object)&&fixture->world.declare_object_word(2,8,0)&&
            fixture->world.set_position(2,{48,32,1,0}),"actual exact collision identity has explicit source Flags8 in bounded source fixture");
        odh::SourceSparsePlacementOutcome failure;failure.status=odh::SourceSparsePlacementStatus::blocked_object;
        failure.blocker=2;failure.blocker_generation=fixture->world.find(2)->generation;failure.placement_error=8;
        fixture->forced_placement=failure;
    }
    blocked.routing.set_outcome_observer([&](const auto& row){blocked_outcomes.push_back(row);});
    blocked.start();blocked_control.start();
    for(std::uint64_t tick=1;tick<256&&(blocked.routing.active()||blocked_control.routing.active());++tick){
        if(blocked.routing.active())require(blocked.routing.tick(tick),("blocked diagnostic route: "+blocked.routing.diagnostic()).c_str());
        if(blocked_control.routing.active())require(blocked_control.routing.tick(tick),("blocked control route: "+blocked_control.routing.diagnostic()).c_str());
    }
    require(!blocked.routing.active()&&!blocked_control.routing.active()&&blocked.vm.status()==blocked_control.vm.status()&&
        blocked.vm.steps()==blocked_control.vm.steps()&&blocked.checks_seen==blocked_control.checks_seen&&
        blocked.world.find(1)->position==blocked_control.world.find(1)->position&&blocked.publications==0,
        "recording first/last actual blocked movement does not add checks, branches or physical movement");
    require(blocked_outcomes.size()==2&&blocked_outcomes.front().failure_code==3&&blocked_outcomes.front().blocker.id==1&&
        blocked_outcomes.front().first_blocked_movement&&blocked_outcomes.front().last_blocked_movement&&
        blocked_outcomes.front().first_blocked_movement->blocker.id==2&&
        blocked_outcomes.front().first_blocked_movement->blocker.generation==blocked.world.find(2)->generation&&
        blocked_outcomes.front().first_blocked_movement->placement_error==8&&
        blocked_outcomes.front().first_blocked_movement->retries_before==5&&
        blocked_outcomes.front().first_blocked_movement->retries_after==4&&
        blocked_outcomes.front().first_blocked_movement->replan.has_value()&&
        blocked_outcomes.front().last_blocked_movement->retries_after<=0&&blocked_outcomes.front().blocked_movement_count>=5,
        "true collider, placement error and existing replan remain distinct from final source NoPath self marker");
    odh_host::RouteOutcomeLog log;odh_host::RouteOutcomeObservation event;
    event.value.actor={1,11};event.value.ticket=7;event.value.run_generation=2;event.value.stage=odh::RouteOutcomeStage::failure_decision;
    event.value.reason="original failure";log.record(event);event.value.reason="later feedback";log.record(event);
    event.value.stage=odh::RouteOutcomeStage::completion;event.value.completion_attempted=true;event.value.completion_accepted=true;log.record(event);
    require(log.rows.size()==1&&log.rows.front().first_failure->value.reason=="original failure"&&log.rows.front().completion->value.completion_accepted,
        "one invocation summary preserves first failure separately from actual completion");
    for(std::uint64_t ticket=8;ticket<50;++ticket){event.value.ticket=ticket;log.record(event);}
    require(log.rows.size()==32&&log.dropped==11,"per-actor bounded summaries count dropped invocations without flooding ticks");
}
void goal_selection_receipt_tests() {
    unsigned provider_calls=0;std::vector<odh::RouteGoalSelectionReceipt> receipts;
    const odh::PreRankedSlotGoalProvider selected=[&](const auto&,std::size_t,const auto&,const auto&){
        ++provider_calls;return std::vector<odh::PreRankedSlotGoal>{{{44,12},4,7}};
    };
    Fixture observed(selected);bool reentry_rejected=false,reconfiguration_rejected=false;
    observed.routing.set_goal_selection_observer([&](const odh::RouteGoalSelectionReceipt& receipt){
        receipts.push_back(receipt);
        // The receipt conveys values, not a route/placement execution lease.
        const auto forged=observed.routing.primitive()(observed.vm.stack().back(),receipt.instruction,
            observed.vm.temps(),{&observed.vm,receipt.ticket});
        reentry_rejected=forged.disposition==odh::PrimitiveDisposition::unsupported;
        try{observed.routing.set_goal_selection_observer({});}
        catch(const std::logic_error&){reconfiguration_rejected=true;}
    });
    observed.start();
    require(provider_calls==1&&receipts.size()==1&&reentry_rejected&&reconfiguration_rejected,
        "ranked-selection observer notifies once without granting reentry or replaying the provider");
    const auto& receipt=receipts.front();
    require(receipt.actor==1&&receipt.target==2&&receipt.actor_generation==observed.world.find(1)->generation&&
            receipt.target_generation==observed.world.find(2)->generation&&receipt.ticket==observed.vm.pending()->ticket&&
            receipt.run_generation==observed.vm.run_generation()&&receipt.routine.id==4096&&receipt.pc==0&&
            receipt.instruction.opcode==45&&receipt.instruction.operands==routines().begin()->second.instructions[0].operands&&
            receipt.actor_position==odh::EntityPosition{4,12,1,0}&&receipt.target_position==odh::EntityPosition{28,12,1,0},
        "receipt retains exact source request and participant identity values");
    require(receipt.backend=="ordinary_ranked"&&!receipt.authenticated&&receipt.circle&&!receipt.range&&!receipt.relative&&
            receipt.provider_completed&&receipt.provider_goals==1&&receipt.accepted_goals==1&&receipt.reachable_attempts==1&&
            receipt.reachability_backend=="ranked_grid_batch"&&receipt.outcome=="selected"&&receipt.reason.empty(),
        "receipt distinguishes returned, accepted and solver-invoked goals without inferring a completed action");
    Fixture control(selected);control.start();
    require(provider_calls==2&&control.vm.steps()==observed.vm.steps()&&control.vm.status()==observed.vm.status()&&
            control.world.find(1)->position==observed.world.find(1)->position,
        "enabling the observer does not add VM execution or provider evaluation");
    for(std::uint64_t tick=1;tick<=64&&(observed.routing.active()||control.routing.active());++tick){
        observed.routing.tick(tick);control.routing.tick(tick);
    }
    require(receipts.size()==1&&provider_calls==2&&observed.vm.status()==control.vm.status()&&
            observed.world.find(1)->position==control.world.find(1)->position,
        "route progress does not emit another selection or change the unobserved outcome");

    receipts.clear();
    Fixture filtered(selected);filtered.routing.set_route_goal_validator([](const auto&,auto){return false;});
    filtered.routing.set_goal_selection_observer([&](const auto& value){receipts.push_back(value);});filtered.start();
    require(receipts.size()==1&&receipts[0].provider_goals==1&&receipts[0].accepted_goals==0&&
            receipts[0].outcome=="unsupported"&&!receipts[0].reason.empty(),
        "receipt separates provider output from later host filtering and retains the original rejection");
    receipts.clear();
    Fixture disconnected(selected);
    for(std::int32_t y=0;y<32;++y)disconnected.grid.block({20,y});
    disconnected.routing.set_goal_selection_observer([&](const auto& value){receipts.push_back(value);});disconnected.start();
    require(receipts.size()==1&&receipts[0].provider_goals==1&&receipts[0].accepted_goals==1&&
            receipts[0].reachable_attempts==1&&receipts[0].outcome=="failed",
        "receipt distinguishes an actual no-path search from a provider that generated no goals");

    receipts.clear();
    odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing relative{world,grid,slots};
    unsigned relative_calls=0;
    relative.set_continuous_ranged_slot_goal_provider([&](const auto&,std::size_t,const auto&,const auto&,
        std::optional<float>){++relative_calls;return std::vector<odh::PreRankedSlotGoal>{};});
    relative.set_route_goal_validator([](const auto&,auto){return true;});
    relative.set_goal_selection_observer([&](const auto& value){receipts.push_back(value);});
    odh::Vm vm{goto_relative({5,0,0,254,0,0,2,0}),world.vm_memory(),{},relative.primitive()};
    world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
    world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});relative.bind(vm);
    vm.start(odh::routine_key(4096),{1,2,2});vm.run(1);
    require(relative_calls==1&&receipts.size()==1&&receipts[0].instruction.opcode==27&&
            receipts[0].backend=="ordinary_continuous"&&receipts[0].relative&&receipts[0].range&&
            receipts[0].provider_completed&&receipts[0].provider_goals==0&&receipts[0].accepted_goals==0&&
            receipts[0].reachable_attempts==0&&receipts[0].outcome=="failed"&&vm.status()==odh::VmStatus::returned_false,
        "empty transient relative provider reports its own failed attempt without inventing a path search");
}
void destination_consultation_tests() {
    const odh::PreRankedSlotGoalProvider selected=[](const auto&,std::size_t,const auto&,const auto&){
        return std::vector<odh::PreRankedSlotGoal>{{{44,12},4,7}};
    };
    Fixture inactive(selected);
    require(inactive.routing.intersects_destination({44,12})==false,
        "a controller without a routing frame has no selected destination");
    Fixture live(selected);live.start();
    require(live.routing.active(),"destination consultation fixture selects an actual route");
    const auto revision=live.world.spatial_revision(),steps=live.vm.steps();
    std::string diagnostic="old failure";
    require(live.routing.intersects_destination({38,6},{},&diagnostic)==true&&diagnostic.empty()&&
            live.routing.intersects_destination({50,18})==true&&
            live.routing.intersects_destination({51,12})==false,
        "selected destination has inclusive six-unit bounds and clears old query diagnostics");
    require(live.world.spatial_revision()==revision&&live.vm.steps()==steps,
        "destination consultation does not execute or mutate the world");
    require(!live.routing.intersects_destination({44,12},std::int16_t{0},&diagnostic)&&
            diagnostic.find("chair identity is invalid")!=std::string::npos,
        "invalid candidate identity remains unknown rather than a free destination");
    Fixture actor_reused(selected);actor_reused.start();
    require(actor_reused.world.remove(1)&&actor_reused.world.create(1,odh::EntityKind::avatar,{0,88,0}),
        "replace a destination route actor identity");
    require(!actor_reused.routing.intersects_destination({44,12},{},&diagnostic)&&
            diagnostic.find("participant identity is stale")!=std::string::npos,
        "destination query refuses actor ID reuse");
    Fixture target_reused(selected);target_reused.start();
    require(target_reused.world.remove(2)&&target_reused.world.create(2,odh::EntityKind::object,{0,0,1}),
        "replace a destination route target identity");
    require(!target_reused.routing.intersects_destination({44,12},{},&diagnostic)&&
            diagnostic.find("participant identity is stale")!=std::string::npos,
        "destination query refuses target ID reuse");
    Fixture restarted(selected);restarted.start();
    restarted.vm.start(odh::routine_key(4096),{1,2,2});
    require(!restarted.routing.intersects_destination({44,12},{},&diagnostic)&&
            diagnostic.find("VM generation is stale")!=std::string::npos,
        "destination query refuses a route from a previous VM run");
}
void source_relative_operand_matrix_tests(){
    const std::array<std::uint8_t,10> values{{254,255,0,1,2,3,4,5,6,7}};
    const auto signed_byte=[](std::uint8_t value)->std::int16_t{return value<=127?value:static_cast<std::int16_t>(value)-256;};
    for(const auto location:values)for(const auto direction:values)for(unsigned flags=0;flags<256;++flags){
        odh::Instruction instruction{27,254,255,{231,127,location,direction,255,254,static_cast<std::uint8_t>(flags),173}};
        const auto decoded=odh::decode_source_goto_relative_position(instruction);
        const auto table=odh::make_source_goto_relative_slot(instruction);
        const auto label="source relative location="+std::to_string(location)+" direction="+std::to_string(direction)+" flags="+std::to_string(flags);
        require(decoded&&decoded->location==signed_byte(location)&&decoded->direction==signed_byte(direction)&&
            decoded->no_failure_trees==((flags&2)!=0)&&table&&table->records.size()==1,
            (label+" ignores unused fields and decodes only source NoFailureTrees").c_str());
        const auto& row=table->records.front();
        const std::array<std::int32_t,3> proximity=location==254?std::array<std::int32_t,3>{0,0,0}:
            location==255?std::array<std::int32_t,3>{16,32,16}:std::array<std::int32_t,3>{16,24,0};
        require(row.type==3&&row.standing==1&&row.sitting==0&&row.ground==0&&row.proximity==proximity&&
            row.flags==(location==254?0U:location==255?255U:1U<<location)&&
            row.facing==(direction==255?-3:signed_byte(direction))&&
            odh::is_source_exact_goto_relative_instruction(instruction)==(location==254)&&
            odh::is_source_ranged_goto_relative_instruction(instruction)==(location!=254)&&
            odh::is_source_ordinary_go_here_instruction(instruction)==(location==254&&direction==255),
            (label+" shares exact/ranged/ordinary geometry without treating unused flags as authority").c_str());
    }
    for(unsigned value=0;value<256;++value){
        odh::Instruction instruction{27,254,255,{0,0,static_cast<std::uint8_t>(value),254,0,0,255,255}};
        const bool defined=value<=7||value>=254;
        require(odh::decode_source_goto_relative_position(instruction).has_value()==defined,
            "undefined signed location values remain explicitly outside admitted source geometry");
        instruction.operands[2]=254;instruction.operands[3]=static_cast<std::uint8_t>(value);
        require(odh::decode_source_goto_relative_position(instruction).has_value()==defined,
            "undefined signed direction values remain explicitly outside admitted source geometry");
    }
}
void source_relative_early_false_tests(){
    Fixture f;require(f.world.create(3,odh::EntityKind::object),"known OOW query target");
    f.routing.set_room_pair_provider([](const auto&,const auto&){
        return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
    f.start();
    require(f.routing.active()&&f.vm.status()==odh::VmStatus::waiting,"real route remains pending during source query probes");
    std::string why;const auto before=f.routing.capture_local_session_route(odh::CodeOwner{0},&why);
    require(before.has_value(),("capture pre-query route: "+why).c_str());
    const auto world_before=f.world.snapshot();const auto vm_before=f.vm.snapshot();
    auto primitive=f.routing.primitive();const auto frame=f.vm.stack().back();
    const odh::Instruction bad_geometry{27,254,255,{255,255,127,128,255,255,255,255}};
    require(primitive(frame,bad_geometry,{},odh::VmDispatch{&f.vm,0,true}).disposition==odh::PrimitiveDisposition::goto_false,
        "source check returns false before unsupported geometry or active-job admission");
    auto nil=frame;require(odh::vm_context_set_reference(nil.context,odh::VmEntityRole::stack_object,{}),"explicit null source pointer");
    require(primitive(nil,bad_geometry,{},odh::VmDispatch{&f.vm,0,false}).disposition==odh::PrimitiveDisposition::goto_false,
        "null StackObject returns false before route providers or operand admission");
    auto oow=frame;require(odh::vm_context_set_reference(oow.context,odh::VmEntityRole::stack_object,{3,f.world.find(3)->generation}),"bind real OOW source target");
    require(primitive(oow,bad_geometry,{},odh::VmDispatch{&f.vm,0,false}).disposition==odh::PrimitiveDisposition::goto_false,
        "OOW StackObject returns false without ending or suspending another active route");
    odh::Vm other({},f.world.vm_memory());
    require(primitive(frame,bad_geometry,{},odh::VmDispatch{&other,0,true}).disposition==odh::PrimitiveDisposition::unsupported,
        "source false does not bypass the controller's VM dispatch identity");
    const auto after=f.routing.capture_local_session_route(odh::CodeOwner{0},&why);
    require(after&&after->ticket==before->ticket&&after->run_generation==before->run_generation&&
        after->path==before->path&&after->next==before->next&&after->expected==before->expected&&
        after->frame.state==before->frame.state&&
        f.vm.steps()==vm_before.steps&&f.vm.pending()->ticket==vm_before.pending->ticket&&
        f.world.snapshot().generation_sequence==world_before.generation_sequence&&
        f.world.find(1)->position==world_before.entities.front().position&&
        f.world.find(1)->static_footprint==world_before.entities.front().static_footprint,
        "check/null/OOW probes preserve existing route, VM, actor placement and footprint");
}
void source_relative_no_failure_ordinary_tests(){
    for(const std::uint8_t flags:{2,6,255}){
        odh::World world;odh::RouteGrid grid{32,32};auto slots=slot();odh::Routing route{world,grid,slots};
        require(world.create(1,odh::EntityKind::avatar,{0,88,0})&&world.create(2,odh::EntityKind::object,{0,0,1})&&
            world.set_position(1,{4,12,1,0})&&world.set_position(2,{12,12,1,0}),"ordinary geometry NoFailureTrees participants");
        auto code=goto_relative({0,0,254,255,0,0,flags,173});
        code[odh::routine_key(398)]={0x8002,{{2,254,255,{1,0,73,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,0,0}};
        odh::Vm vm{std::move(code),world.vm_memory(),{},route.primitive()};
        route.set_route_goal_validator([](const auto&,auto){return true;});
        route.set_room_pair_provider([](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,2}};});
        route.set_route_grid_refresh([](const auto&){return true;});
        unsigned plans=0;route.set_room_portal_step_provider([&](auto,const auto&,const auto&,const auto&,const auto&,auto){
            ++plans;return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"no portal path"};});
        require(route.bind(vm)&&vm.start(odh::routine_key(4096),{1,2,2})==odh::VmStatus::running&&
            vm.run(1)==odh::VmStatus::waiting&&route.waiting_for_room_portal_step(),
            "OnTopOf/AnyDirection with NoFailureTrees waits for actual room traversal");
        std::string why;const auto captured=route.capture_session_route_stack(0,&why);
        require(captured&&captured->jobs.size()==1&&!captured->jobs.front().call_failure_trees,
            ("ordinary GoHere geometry preserves bit2 in pending route flags="+std::to_string(flags)+": "+why).c_str());
        require(route.tick(1)&&plans==1&&!route.active()&&vm.status()==odh::VmStatus::returned_false&&vm.temps()[1]==0,
            "flags2/6/ff return false after portal failure without invoking installed Global398");
    }
}
void deferred_relative_destination_tests() {
    // All these source27 profiles construct a transient SLOT before room
    // traversal. Parsed old counters vary; none selects CurRoute early.
    const std::vector<std::array<std::uint8_t,8>> profiles{
        {5,0,0,254,0,0,0,0}, {231,3,0,254,127,1,3,0},
        {5,0,255,255,0,0,2,0}, {5,0,7,3,0,0,1,0},
        {5,0,254,254,0,0,0,0}, {10,0,254,255,10,0,5,0},
        {0,0,0,254,0,0,6,0}, {231,127,254,255,255,254,253,173},
        {231,127,255,4,255,254,255,173}};
    for(const auto operands:profiles){
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing route{world,grid,slots};
        unsigned goal_calls=0;
        // The live host supplies both ordinary continuous range scoring and
        // its authenticated45 provider. Deferred27 admission still verifies
        // the former capability before it queues room traversal.
        route.set_continuous_ranged_slot_goal_provider([&](const auto&,std::size_t,const auto&,
            const auto&,std::optional<float>)->std::vector<odh::PreRankedSlotGoal>{
            ++goal_calls;throw std::runtime_error("deferred query must not score a relative SLOT");
        });
        route.set_route_goal_validator([](const auto&,auto){return true;});
        route.set_authenticated_range_slot_goal_provider([&](const auto&,std::size_t,const auto&,
            const auto&,float,const auto&)->odh::AuthenticatedRangeSlotGoals{
            ++goal_calls;throw std::runtime_error("deferred query must not select a destination");
        });
        route.set_room_pair_provider([](const auto&,const auto&)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>>{return std::pair{3,1};});
        route.set_route_grid_refresh([](const auto&){return true;});
        route.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto){
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
                odh::VmCall{odh::routine_key(4097),{1,2,2,9},{}},{}};
        });
        odh::RoutineSet portal{{odh::routine_key(4097),odh::Bhav{0x8002,
            {{45,254,255,{1,0,2,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
        odh::Vm vm{goto_relative(operands),world.vm_memory(),{},route.primitive(),{{9,portal}}};
        require(world.create(1,odh::EntityKind::avatar,{0,88,0})&&world.create(2,odh::EntityKind::object,{0,0,1})&&
                world.set_position(1,{4,12,1,0})&&world.set_position(2,{28,12,1,0})&&route.bind(vm),
                "deferred transient relative SLOT world and Thread bound");
        vm.start(odh::routine_key(4096),{1,2,2});
        const auto first_status=vm.run(1);
        const auto profile="relative location="+std::to_string(operands[2])+" direction="+
            std::to_string(operands[3])+" flags="+std::to_string(operands[6]);
        require(first_status==odh::VmStatus::waiting&&route.active()&&goal_calls==0,
                (profile+" enters room traversal before destination parsing: "+vm.diagnostic()).c_str());
        std::string why;
        require(route.intersects_destination({28,12},{},&why)==false&&why.empty()&&goal_calls==0,
                "ranged, exact and ordinary relative SLOTs have proven CurRoute null before room traversal");
        const auto initial=route.capture_session_route_stack(0,&why);
        require(initial&&initial->jobs.size()==1&&initial->jobs.front().deferred_room_route&&
                initial->jobs.front().replay_instruction->operands==operands&&
                initial->jobs.front().face_anywhere==(operands[3]==255)&&
                initial->jobs.front().call_failure_trees==((operands[6]&2U)==0),
                "deferred relative capture retains exact raw operands and source facing/failure policy");
        require(route.tick(1)&&route.external_frame_active()&&vm.run(1)==odh::VmStatus::waiting&&
                route.active_route_depth()==2&&route.intersects_destination({28,12})==false,
                "relative parent and nested SLOT child both retain no selected destination");
        auto state=route.capture_session_route_stack(0,&why);
        require(state&&state->jobs.size()==2,
                "transient relative parent with nested portal SLOT captures its whole stack");
        // Routing captures its frames only. The real host separately appends
        // its per-ticket room contexts before restore. This synthetic planner
        // has no ignored architectural edge; retain each open context ticket.
        for(const auto& job:state->jobs)if(job.room_context_open){
            odh::RoutePortalContextSnapshot context;context.ticket=job.ticket;
            state->portal_contexts.push_back(std::move(context));
        }
        odh::World resumed_world;require(resumed_world.restore_snapshot(world.snapshot()),"relative replay world restored");
        odh::RouteGrid resumed_grid{64,32};odh::Routing resumed{resumed_world,resumed_grid,slots};
        resumed.set_continuous_ranged_slot_goal_provider([&](const auto&,std::size_t,const auto&,
            const auto&,std::optional<float>)->std::vector<odh::PreRankedSlotGoal>{
            ++goal_calls;throw std::runtime_error("restore/query must not score a relative SLOT");
        });
        resumed.set_route_goal_validator([](const auto&,auto){return true;});
        resumed.set_authenticated_range_slot_goal_provider([&](const auto&,std::size_t,const auto&,
            const auto&,float,const auto&)->odh::AuthenticatedRangeSlotGoals{
            ++goal_calls;throw std::runtime_error("restore/query must not select a destination");
        });
        resumed.set_room_pair_provider([](const auto&,const auto&)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>>{return std::pair{3,1};});
        resumed.set_route_grid_refresh([](const auto&){return true;});
        resumed.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto){
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"not advanced by destination query"};
        });
        odh::Vm resumed_vm{goto_relative(operands),resumed_world.vm_memory(),{},resumed.primitive(),{{9,portal}}};
        require(resumed.bind(resumed_vm)&&resumed_vm.restore_snapshot(vm.snapshot()),
                "relative restore binds an equivalent fresh VM");
        auto invalid=*state;invalid.jobs.front().face_anywhere=!invalid.jobs.front().face_anywhere;
        require(!resumed.restore_session_route_stack(invalid,0,&why)&&!resumed.active()&&
                why.find("source facing or failure policy changed")!=std::string::npos,
                "fresh deferred restore rejects altered source policy atomically");
        const bool restored=resumed.restore_session_route_stack(*state,0,&why);
        const auto restored_destination=restored?resumed.intersects_destination({28,12}):std::optional<bool>{};
        require(restored&&restored_destination==false&&goal_calls==0,
                (profile+" fresh restore authenticates no-CurRoute without reparsing: "+why+
                 " restored="+std::to_string(restored)+" goal_calls="+std::to_string(goal_calls)).c_str());
    }
    odh::Instruction valid{27,254,255,{5,0,0,254,0,0,0,0}};
    require(odh::is_source_ranged_goto_relative_instruction(valid),"actual F1 transient relative instruction is shared");
    for(const auto field:std::array<std::pair<unsigned,std::uint8_t>,2>{{{2,8},{3,8}}}){
        auto invalid=valid;invalid.operands[field.first]=field.second;
        require(!odh::is_source_ranged_goto_relative_instruction(invalid),
                "shared transient ranged predicate rejects undefined location and direction");
    }
}
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
    first_routing.set_room_pair_provider([](const auto&,const auto&)
        ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {return std::pair{1,1};});
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
    require(request_go_here_fixture(first_session,world,first_vm,{12,4,1,0},501,goto_relative({10,0,254,255,10,0,5,0}),4096,true)&&
            request_go_here_fixture(second_session,world,second_vm,{28,20,1,0},502,goto_relative()),
            "each resident queues a Go Here marker and action under its own code owner");
    require(first_session.marker_id()&&second_session.marker_id()&&
            first_session.marker_id()!=second_session.marker_id()&&first_session.marker_alive()&&
            second_session.marker_alive(),"resident Go Here sessions own distinct live World markers");
    std::uint64_t checkpoint_tick=0;
    while(checkpoint_tick<4&&!first_routing.active()){
        ++checkpoint_tick;first_routing.tick(checkpoint_tick);second_routing.tick(checkpoint_tick);
        require(first_threads.advance(checkpoint_tick)&&second_threads.advance(checkpoint_tick),
                "resident checkpoint controllers advance on consecutive ticks");
        first_vm.run(8);second_vm.run(8);first_threads.observe();second_threads.observe();
    }
    require(first_routing.active(),"resident Go Here fixture reaches an active restored-route checkpoint");
    std::string capture_rejection;
    const auto saved_route=first_routing.capture_local_session_route(501,&capture_rejection);
    require(saved_route.has_value(),("active resident Go Here route yields a validated local snapshot: "+capture_rejection).c_str());
    odh::SessionDynamicActionOwner binding;binding.owner=501;binding.target_id=saved_route->target;
    binding.target_generation=saved_route->target_generation;binding.target_guid=0x7c4;
    binding.root_routine=4096;
    const auto before_restore=world.snapshot();
    odh_host::HouseholdGoHereSession restored_session{world,11,first_vm,first_threads,first_routing};
    require(restored_session.restore(*saved_route,binding)&&restored_session.status()=="queued"&&
            restored_session.marker_id()==saved_route->target&&restored_session.owner()==501&&
            restored_session.destination()==saved_route->target_position,
            "already-waiting resident Go Here route rehydrates its marker and action ownership");
    const auto saved_actor=std::find_if(before_restore.entities.begin(),before_restore.entities.end(),
        [](const auto& entity){return entity.id==11;});
    const auto preserves_world=[&]{
        const auto after=world.snapshot();
        const auto actor_now=world.find(11);
        return saved_actor!=before_restore.entities.end()&&after.entities.size()==before_restore.entities.size()&&
            world.find(saved_route->target)&&world.find(saved_route->target)->generation==saved_route->target_generation&&
            actor_now&&actor_now->position==saved_actor->position;
    };
    auto wrong_actor=*saved_route;wrong_actor.actor=21;
    odh_host::HouseholdGoHereSession reject_actor{world,11,first_vm,first_threads,first_routing};
    require(!reject_actor.restore(wrong_actor,binding)&&preserves_world(),
            "Go Here restore rejects a route for another actor without changing restored World");
    auto wrong_marker=*saved_route;++wrong_marker.target_generation;
    odh_host::HouseholdGoHereSession reject_marker{world,11,first_vm,first_threads,first_routing};
    require(!reject_marker.restore(wrong_marker,binding)&&preserves_world(),
            "Go Here restore rejects a different marker generation without changing restored World");
    auto wrong_owner=binding;wrong_owner.owner=999;
    odh_host::HouseholdGoHereSession reject_owner{world,11,first_vm,first_threads,first_routing};
    require(!reject_owner.restore(*saved_route,wrong_owner)&&preserves_world(),
            "Go Here restore rejects an unbound dynamic owner without changing restored World");
    auto wrong_profile=*saved_route;wrong_profile.call_failure_trees=false;
    odh_host::HouseholdGoHereSession reject_profile{world,11,first_vm,first_threads,first_routing};
    require(!reject_profile.restore(wrong_profile,binding)&&preserves_world(),
            "Go Here restore rejects a non-Go Here route profile without adopting its marker");
    auto wrong_ticket=*saved_route;++wrong_ticket.ticket;
    odh_host::HouseholdGoHereSession reject_ticket{world,11,first_vm,first_threads,first_routing};
    require(!reject_ticket.restore(wrong_ticket,binding)&&preserves_world(),
            "Go Here restore rejects a nonmatching pending VM ticket without changing restored World");
    for(std::uint64_t tick=checkpoint_tick+1;tick<=checkpoint_tick+32&&
        (first_session.status()=="queued"||second_session.status()=="queued");++tick){
        if(first_routing.active())first_routing.tick(tick);
        if(second_routing.active())second_routing.tick(tick);
        require(first_threads.advance(tick)&&second_threads.advance(tick),
                "resident continuation controllers advance on consecutive ticks");
        first_vm.run(8);second_vm.run(8);
        first_threads.observe();second_threads.observe();
        restored_session.observe();first_session.observe();second_session.observe();
    }
    require(first_session.status()=="returned_true"&&second_session.status()=="returned_true"&&
            first_session.owner()==501&&second_session.owner()==502&&first_session.result()==true&&
            second_session.result()==true&&restored_session.status()=="returned_true"&&
            !first_session.marker_alive()&&!restored_session.marker_alive()&&second_session.marker_alive()&&
            world.find(11)->position->x==12&&world.find(11)->position->y==4&&
            world.find(21)->position->x==28&&world.find(21)->position->y==20,
            (std::string("interleaved resident Go Here completion: first=")+first_session.status()+
                " restored="+restored_session.status()+" second="+second_session.status()+
                " firstVM="+odh::vm_status_name(first_vm.status())+" secondVM="+odh::vm_status_name(second_vm.status())+
                " firstRoute="+first_routing.diagnostic()+" secondRoute="+second_routing.diagnostic()+
                " firstPosition="+std::to_string(world.find(11)->position->x)+","+
                std::to_string(world.find(11)->position->y)+" secondPosition="+
                std::to_string(world.find(21)->position->x)+","+std::to_string(world.find(21)->position->y)).c_str());
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
    require(request_go_here_fixture(failed_session,world,failed_vm,{28,28,1,0},503,goto_relative())&&failed_session.marker_alive(),
            "host-failure fixture creates a queued request and owned marker");
    failed_session.observe(true,"synthetic resident route tick failure");
    require(failed_session.status()=="host_failed"&&failed_session.marker_alive(),
            "host route failure leaves ordinary Destination lifecycle owned by its Main");
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
    require(request_go_here_fixture(incomplete_session,world,incomplete_vm,{28,29,1,0},504,goto_relative())&&incomplete_session.marker_alive(),
            "incomplete resident fixture creates a queued request and owned marker");
    incomplete_session.finish_incomplete("tick_limit","synthetic route tick budget ended");
    require(incomplete_session.status()=="tick_limit"&&incomplete_session.route_status()=="incomplete"&&
            incomplete_session.marker_alive(),
            "tick-limited request finalizes tracking without deleting the ordinary Destination");
}
void household_go_here_stack_restore_tests() {
    odh::World world;odh::RouteGrid grid{32,16};auto slots=slot();
    require(world.create(11,odh::EntityKind::avatar,{0,88,0})&&world.set_position(11,{4,4,1,0}),
        "stack Go Here creates actor");
    const odh::RoutineSet main{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{17,254,255,{0,0,1,0}}},{},odh::Bhav::Metadata{0,1,0,0}}}};
    const auto action=goto_relative({10,0,254,255,10,0,5,0});
    const odh::RoutineSet portal{{odh::routine_key(4097),odh::Bhav{0x8002,
        {{45,254,255,{1,0,2,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    odh::EntityThreadControllers threads{world,11};
    unsigned plans=0;
    odh::Routing route{world,grid,slots,[&](const auto&,auto,const auto&,const auto& target){
        ++plans;return std::vector<odh::PreRankedSlotGoal>{{{8,4},
            odh::reference_slot_entry_flags({target.x,target.y},target.direction,{8,4},0x55),2}};}};
    const auto configure=[](odh::Routing& routing){
        routing.set_room_pair_provider([](const auto&,const auto&){
            return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
        routing.set_route_goal_validator([](const auto&,auto){return true;});
    };configure(route);
    odh::Vm vm{main,world.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        return i.opcode==17?threads.interaction_primitive()(f,i,t,d):route.primitive()(f,i,t,d);
    },{{601,portal}}};
    require(threads.bind(vm)&&route.bind(vm),"stack Go Here binds live controllers");
    vm.start(odh::routine_key(4096),{11,11,11,0},{120});
    require(vm.run(4)==odh::VmStatus::waiting,"stack Go Here main waits for queue");
    odh_host::HouseholdGoHereSession original{world,11,vm,threads,route};
    require(request_go_here_fixture(original,world,vm,{12,4,1,0},501,action,4096,true)&&threads.advance(1)&&vm.run(8)==odh::VmStatus::waiting&&threads.observe(),
        "stack Go Here activates its one queued action");
    const auto root_ticket=vm.pending()->ticket;
    require(route.call_external_frame({odh::routine_key(4097),{11,*original.marker_id(),*original.marker_id(),601},{}})&&
        vm.run(1)==odh::VmStatus::waiting&&route.tick(2),"stack Go Here suspends root for a moving child");
    std::string why;const auto stack=route.capture_session_route_stack(501,&why);
    require(stack&&stack->jobs.size()==2&&stack->jobs.front().ticket==root_ticket&&
        !(stack->jobs.front().expected==*world.find(11)->position),"root expected pose remains historical after child moves");
    odh::SessionDynamicActionOwner binding;binding.owner=501;binding.target_id=*original.marker_id();
    binding.target_generation=*original.marker_generation();binding.target_guid=0x7c4;binding.root_routine=4096;
    odh::World fresh;require(fresh.restore_snapshot(world.snapshot()),"stack Go Here restores World into fresh graph");
    odh::RouteGrid fresh_grid{32,16};unsigned fresh_plans=0;
    odh::Routing restored{fresh,fresh_grid,slots,[&](const auto&,auto,const auto&,const auto&){
        ++fresh_plans;return std::vector<odh::PreRankedSlotGoal>{};}};configure(restored);
    odh::EntityThreadControllers fresh_threads{fresh,11};
    odh::Vm fresh_vm{main,fresh.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        return i.opcode==17?fresh_threads.interaction_primitive()(f,i,t,d):restored.primitive()(f,i,t,d);
    },{{501,action},{601,portal}}};
    require(fresh_threads.bind(fresh_vm)&&restored.bind(fresh_vm)&&fresh_vm.restore_snapshot(vm.snapshot())&&
        fresh_threads.restore_snapshot(threads.snapshot())&&restored.restore_session_route_stack(*stack,501,&why),
        "stack Go Here restores actual core/VM/queue before host adoption");
    const auto before=fresh.snapshot();const auto vm_before=fresh_vm.snapshot();
    const auto rejected=[&](const odh::RouteStackSnapshot& value,const odh::SessionDynamicActionOwner& owner){
        odh_host::HouseholdGoHereSession session{fresh,11,fresh_vm,fresh_threads,restored};
        require(!session.restore(value,owner)&&!session.marker_id()&&session.status()=="not_requested"&&
            fresh.find(binding.target_id)&&fresh.find(binding.target_id)->generation==binding.target_generation&&
            fresh.snapshot().spatial_revision==before.spatial_revision&&fresh_vm.steps()==vm_before.steps,
            "rejected stack host adoption has no marker, VM or World side effects");
    };
    auto bad=*stack;++bad.jobs.front().pending.ticket;rejected(bad,binding);
    bad=*stack;bad.jobs.front().route_frame.arguments.push_back(17);rejected(bad,binding);
    bad=*stack;++bad.jobs.front().target_generation;rejected(bad,binding);
    bad=*stack;bad.jobs.erase(bad.jobs.begin());rejected(bad,binding);
    bad=*stack;bad.jobs.clear();rejected(bad,binding);
    auto wrong_owner=binding;++wrong_owner.owner;rejected(*stack,wrong_owner);
    wrong_owner=binding;wrong_owner.root_routine=4097;rejected(*stack,wrong_owner);
    odh_host::HouseholdGoHereSession adopted{fresh,11,fresh_vm,fresh_threads,restored};
    require(adopted.restore(*stack,binding)&&adopted.status()=="queued"&&adopted.route_status()=="pending"&&
        adopted.marker_id()==original.marker_id()&&adopted.owner()==501&&
        adopted.start()==fresh.find(11)->position&&adopted.destination()==original.destination()&&fresh_plans==0&&plans==1,
        "fresh stack Go Here adopts original marker/root owner without reranking or confusing child pending with root");
}
void immediate_route_controller_restore(){
    struct Graph {
        odh::World world;odh::RouteGrid grid{32,16};odh::SlotTable slots;
        odh::Routing route{world,grid,slots};odh::EntityThreadControllers threads{world,11};odh::Vm vm;
        static odh::RoutineSet program(){return {
            {odh::routine_key(4096),{0x8002,{{27,1,255,{5,0,254,255,0,0,2,0}},
                {2,254,255,{0,0,91,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,4,0}}},
            {odh::routine_key(4101),{0x8002,{{42,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}};}
        Graph(bool populate=true):vm(program(),world.vm_memory(),{},[this](const auto& f,const auto& i,const auto& t,const auto& d){
            if(i.opcode==42)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
            if(i.opcode==0)return threads.sleep_primitive()(f,i,t,d);
            if(i.opcode==17)return threads.interaction_primitive()(f,i,t,d);
            return route.primitive()(f,i,t,d);
        }){
            if(populate)require(world.create(11,odh::EntityKind::avatar,{0,88,0})&&
                world.create(20,odh::EntityKind::object)&&world.set_position(11,{4,4,1,0})&&
                world.set_position(20,{12,4,1,0})&&world.declare_object_word(11,8,16),
                "create actual immediate route/controller graph");
            route.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
            route.set_route_goal_validator([](const auto&,auto){return true;});
            require(threads.set_immediate_service({
                [this](const auto& p,const auto& f){return route.can_park_for_immediate(p,f);},
                [this](auto ticket){return route.park_for_immediate(ticket);},
                [this](auto ticket){return route.retry_immediate_parent(ticket);},
                [this](const auto& p,const auto& f){return route.validates_immediate_parent(p,f);}})&&
                (!populate||threads.bind(vm))&&route.bind(vm),"bind full immediate Routing service to real thread controller");
        }
    };
    Graph live;
    require(live.vm.start(odh::routine_key(4096),{11,20,20,0})==odh::VmStatus::running&&
        live.vm.run(10)==odh::VmStatus::waiting&&
        live.threads.interactions().enqueue_source_ts1(odh::routine_key(4101),{11,20,20,0},{},100,
            odh::InteractionQueueMode::normal,4,false,false),"actual immediate action queues over original Main route");
    require(live.threads.advance(1)&&live.route.parked_for_immediate()&&live.route.tick(1)&&
        live.vm.run(10)==odh::VmStatus::waiting&&live.vm.pending()->instruction.instruction.opcode==42,
        "controller pre-tick selects immediate before parent movement and child reaches independent async wait");
    require(!live.route.can_release_idle_cache(),"an immediate action still pins its covered parent routing cache");
    std::string reason;const auto saved=live.route.capture_session_route_stack(0,&reason);
    if(!saved)throw std::runtime_error("controller parked route capture: "+reason);
    Graph resumed(false);
    require(resumed.world.restore_snapshot(live.world.snapshot())&&resumed.vm.restore_snapshot(live.vm.snapshot())&&resumed.threads.bind(resumed.vm)&&
        resumed.threads.restore_snapshot(live.threads.snapshot())&&
        resumed.route.restore_session_route_stack(*saved,0,&reason),
        "actual mid-child World VM controllers and Routing restore before any execution");
    for(auto* graph:{&live,&resumed}){
        require(graph->vm.complete(graph->vm.pending()->ticket,true)&&graph->vm.run(10)==odh::VmStatus::yielded&&
            graph->threads.observe()&&!graph->route.parked_for_immediate()&&graph->vm.status()==odh::VmStatus::waiting,
            "controller completion acknowledges immediate and resumes original Routing service exactly once");
        for(std::uint64_t tick=2;graph->route.active()&&tick<40;++tick){
            require(graph->threads.advance(tick)&&graph->route.tick(tick),"advance resumed parent with real controller clock");
            if(graph->vm.status()==odh::VmStatus::running||graph->vm.status()==odh::VmStatus::budget_exhausted)graph->vm.run(10);
            require(graph->threads.observe(),"observe parent continuation after immediate completion");
        }
        require(!graph->route.active()&&graph->vm.status()==odh::VmStatus::returned_true&&graph->vm.temps()[0]==91,
            "resumed parent reaches goal and authored Main branch");
    }
    require(live.world.find(11)->position==resumed.world.find(11)->position&&
        live.world.find(11)->radian_direction==resumed.world.find(11)->radian_direction&&
        live.vm.temps()==resumed.vm.temps(),"real controller restore preserves final pose and thread registers");
}
void ordinary_go_here_factory_tests() {
    odh::World world;odh::RouteGrid grid{32,16};auto slots=slot();
    require(world.create(11,odh::EntityKind::avatar,{0,88,0})&&world.set_position(11,{4,4,1,0}),
        "ordinary Go Here creates a real actor");
    odh::EntityThreadControllers threads{world,11};odh::Routing routing{world,grid,slots};
    const odh::RoutineSet main{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{17,0,0,{0,0,1,0}}},{},odh::Bhav::Metadata{0,1,0,0}}}};
    auto routines=goto_relative({10,0,254,255,10,0,5,0});
    routines.emplace(odh::routine_key(4109),odh::Bhav{0x8002,{{2,254,255,{0,0,0,0,0,5,8,7}}},{},odh::Bhav::Metadata{0,4,0,0}});
    const auto program=std::make_shared<const odh::VmProgram>(routines);
    odh::Vm vm{main,world.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        return i.opcode==17?threads.interaction_primitive()(f,i,t,d):routing.primitive()(f,i,t,d);}};
    require(threads.bind(vm)&&routing.bind(vm)&&vm.install_shared_owner(501,program),"install exact immutable ordinary Destination bank");
    routing.set_room_pair_provider([](const auto&,const auto&){return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{1,1}};});
    routing.set_route_goal_validator([](const auto&,auto){return true;});
    vm.start(odh::routine_key(4096),{11,11,11,0},{120});require(vm.run(4)==odh::VmStatus::waiting,"ordinary Go Here parks Main");
    using Session=odh_host::HouseholdGoHereSession;
    Session::ConstructedDestination receipt;unsigned factories=0;std::function<bool()> expired;
    Session::Services services;services.source_still_matches=[](){return true;};
    services.construct=[&](const auto& destination,const auto& current,std::string*)->std::optional<Session::ConstructedDestination>{
        ++factories;expired=current;if(!current())return {};
        require(world.create_runtime_object(20,0x7c4,{},std::vector<odh::EntryPoint>(31))&&world.set_position(20,destination),
            "injected ordinary factory publishes its initialized object");
        odh::Interaction wrong{};wrong.label_index=5;wrong.action=4112;wrong.flags=0x601;
        odh::Interaction go{};go.label_index=2;go.action=4096;go.test=4109;go.flags=0x621;
        receipt={20,world.find(20)->generation,501,program,odh::Ttab{odh::TtabStatus::decoded,2,9,true,0,{wrong,go}}};return receipt;
    };
    {
        Session request{world,11,vm,threads,routing};
        require(request.request({12,4,1,0},services,17)&&factories==1&&expired&&!expired(),
            "Go Here consumes the real factory exactly once and expires its authority after the command");
        const auto q=threads.interactions().snapshot();const auto& action=q.actions.front();
        require(action.interaction_number==std::optional<std::int32_t>{2}&&action.priority==50&&action.flags==0x621&&
            action.check&&action.check->id==4109&&action.check_permissions&&
            action.argument_mode==odh::InteractionArgumentMode::source_explicit&&action.call.exact_arguments&&
            action.call.arguments==std::vector<std::int16_t>{17,0,0,0}&&action.call.context.code_owner==501&&
            action.call.context.caller==11&&action.call.context.callee==20&&action.call.context.stack_object==20,
            "Go Here selects semantic2 rather than ordinal row, with explicit four args and real owner/check/flags");
        require(threads.interactions().enqueue_source_user(odh::routine_key(4109),{11,20,20,501},
            std::vector<std::int16_t>{0,0,0,0},4,{},99)&&threads.advance(1)&&
            vm.run(16)==odh::VmStatus::yielded&&threads.observe(),
            "real immediate child completes before the queued Go Here command starts");
        require(threads.interactions().completion()&&threads.interactions().completion()->uid!=action.uid,
            "immediate child publishes a distinct new completion");
        request.observe();
        require(request.status()=="queued"&&request.marker_alive(),
            "an unrelated real completion cannot terminate or remove the ordinary Go Here command");
        require(!world.find(20)->ui_ghost&&world.find(20)->static_footprint&&threads.advance(2)&&
            vm.run(16)==odh::VmStatus::waiting&&threads.observe()&&routing.active(),
            "ordinary Destination admits actual queued action and reaches its route");
        require(world.create_runtime_object(21,0x7c4,{},std::vector<odh::EntryPoint>(31))&&world.set_position(21,{28,4,1,0})&&
            threads.interactions().enqueue_source_user(odh::routine_key(4096),{11,21,21,501},
                std::vector<std::int16_t>{0,0,0,0},0x621,odh::routine_key(4109),2)&&
            threads.interactions().snapshot().actions.size()==2,
            "another valid ordinary Destination user action queues behind active Go Here");
        std::string error;const auto stack=routing.capture_session_route_stack(501,&error);
        require(stack&&stack->jobs.size()==1,"ordinary Go Here captures its actual routing graph");
        Session::CreatedDestinationBinding binding;binding.destination=receipt;
        binding.origin={20,receipt.generation,0x7c4,"Objects.far","Destination.iff",16807};
        binding.resource_owner.owner=501;binding.resource_owner.resource_guid=0x7c4;
        binding.resource_owner.source="Objects.far";binding.resource_owner.asset="Destination.iff";
        binding.resource_owner.definition_chunk_id=16807;binding.resource_owner.tree_table_id=128;
        binding.resource_owner.root_routine=4096;binding.resource_owner.bank_kind=odh::SessionCodeBankKind::complete_private;
        odh::SessionDynamicActionOwner legacy;legacy.owner=501;legacy.target_id=20;legacy.target_generation=receipt.generation;
        legacy.target_guid=0x7c4;legacy.root_routine=4096;
        const auto before=world.snapshot();const auto steps=vm.steps();
        Session rejected_legacy{world,11,vm,threads,routing};
        require(!rejected_legacy.restore(*stack,legacy),"legacy restore cannot adopt an ordinary object by relaxing its ghost guards");
        const auto rejects=[&](const auto& bad){Session restored{world,11,vm,threads,routing};
            require(!restored.restore_created(*stack,bad)&&!restored.marker_id()&&world.find(20)&&
                world.snapshot().spatial_revision==before.spatial_revision&&vm.steps()==steps,
                "bad ordinary provenance cannot acquire lifecycle ownership or mutate World/VM");};
        auto bad=binding;++bad.origin.generation;rejects(bad);bad=binding;bad.resource_owner.asset="Other.iff";rejects(bad);
        bad=binding;bad.destination.program=std::make_shared<const odh::VmProgram>(routines);rejects(bad);
        bad=binding;bad.destination.interactions->interactions.back().label_index=3;rejects(bad);
        {
            auto wrong=threads.interactions().snapshot();
            const auto actual=std::find_if(wrong.actions.begin(),wrong.actions.end(),[&](const auto& item){return item.uid==wrong.active->uid;});
            actual->interaction_number=3;
            odh::EntityThreadControllers shadow{world,11};
            require(shadow.interactions().bind(vm)&&shadow.interactions().restore_snapshot(wrong),
                "native queue validates frame links while retaining a different immutable active interaction number");
            Session rejected{world,11,vm,shadow,routing};
            require(!rejected.restore_created(*stack,binding)&&!rejected.marker_id()&&world.find(20),
                "a later matching Go Here action cannot substitute for mismatched active command metadata");
        }
        {
            Session restored{world,11,vm,threads,routing};
            require(restored.restore_created(*stack,binding)&&restored.marker_id()==20&&factories==1&&vm.steps()==steps&&
                world.snapshot().spatial_revision==before.spatial_revision,
                "authenticated real Go Here restores tracking without rerunning factory, Init, queue or route planning");
        }
        require(world.find(20),"restored ordinary Destination survives tracking destructor");
        for(std::uint64_t tick=3;tick<40&&request.status()=="queued";++tick){
            if(routing.active())routing.tick(tick);require(threads.advance(tick),"ordinary controller tick");
            vm.run(16);require(threads.observe(),"ordinary controller observation");request.observe();}
        require(request.status()=="returned_true"&&world.find(20)&&!world.find(20)->ui_ghost,
            "ordinary Go Here completion keeps object Main alive instead of marker cleanup");
    }
    require(world.find(20),"ordinary object survives request destructor");
    auto followup_tick=threads.interactions().snapshot().clock;
    while(threads.interactions().size()&&followup_tick<120){
        ++followup_tick;if(routing.active())routing.tick(followup_tick);
        require(threads.advance(followup_tick),"queued follow-up controller tick");
        vm.run(16);require(threads.observe(),"queued follow-up completion remains source valid");
    }
    require(threads.interactions().size()==0&&world.find(11)->position->x==28,
        "the independently queued user action continues after Go Here tracking completes");
    require(world.remove(20)&&world.remove(21),"owning fixture Mains retire ordinary Destinations independently");
    {
        Session rejected{world,11,vm,threads,routing};Session::Services absent;
        require(!rejected.request({20,4,1,0},absent)&&factories==1,"missing real factory cannot fall back to ghost construction");
    }
    {
        Session rejected{world,11,vm,threads,routing};auto unknown=services;
        unknown.source_still_matches=[](){return false;};const auto before=world.snapshot();
        require(!rejected.request({20,4,1,0},unknown)&&factories==1&&world.snapshot().generation_sequence==before.generation_sequence,
            "unknown command authority cannot invoke ordinary construction");
    }
    {
        Session rejected{world,11,vm,threads,routing};auto malformed=services;
        const auto factory=services.construct;
        malformed.construct=[&](const auto& p,const auto& current,auto* why){auto object=factory(p,current,why);
            object->interactions->interactions.back().label_index=5;return object;};
        require(!rejected.request({20,4,1,0},malformed)&&threads.interactions().size()==0&&world.find(20),
            "malformed semantic TTAB cannot enqueue guessed action and cannot delete the factory Main");
        require(world.remove(20),"fixture Main owns malformed-request object retirement");
    }
    {
        Session rejected{world,11,vm,threads,routing};auto oow=services;unsigned retire_calls=0;
        oow.construct=[&](const auto&,const auto& current,auto*)->std::optional<Session::ConstructedDestination>{
            require(current()&&world.create_runtime_object(20,0x7c4,{},std::vector<odh::EntryPoint>(31)),
                "fixture ordinary placement rejection leaves initialized Destination OOW");
            auto object=receipt;object.generation=world.find(20)->generation;return object;};
        oow.retire_failed_placement=[&](auto id,auto generation,auto* why){++retire_calls;
            require(id==20&&generation==world.find(20)->generation,"failed-placement retirement uses exact generation");
            *why="source deletion lifecycle unavailable";return false;};
        require(!rejected.request({20,4,1,0},oow)&&retire_calls==1&&world.find(20)&&!world.find(20)->position&&
            threads.interactions().size()==0&&rejected.diagnostic()=="source deletion lifecycle unavailable",
            "unsupported OOW deletion never falls back to direct World removal or queues a fabricated callee");
    }
}
void ordinary_go_here_pending_restore_tests() {
    using Session=odh_host::HouseholdGoHereSession;
    odh::World world;odh::RouteGrid grid{32,16};auto slots=slot();
    require(world.create(11,odh::EntityKind::avatar,{0,88,0})&&world.set_position(11,{4,4,1,0}),"pending Go Here actor");
    const odh::RoutineSet main{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{17,0,0,{0,0,1,0}}},{},odh::Bhav::Metadata{0,1,0,0}}}};
    auto action=goto_relative({10,0,254,255,10,0,5,0});
    action.at(odh::routine_key(4096)).instructions.insert(action.at(odh::routine_key(4096)).instructions.begin(),
        {2,1,255,{0,0,99,0,0,5,9,7}}); // Actual BHAV: Param0 := 99 before first route.

    odh::Routing routing{world,grid,slots};odh::EntityThreadControllers threads{world,11};
    odh::Vm vm{main,world.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        return i.opcode==17?threads.interaction_primitive()(f,i,t,d):routing.primitive()(f,i,t,d);},
        {{501,action}}};
    const auto installed=vm.shared_program_for_owner(501);
    require(threads.bind(vm)&&routing.bind(vm),"pending Go Here binds its real controllers");
    vm.start(odh::routine_key(4096),{11,11,11,0},{120});require(vm.run(4)==odh::VmStatus::waiting,"pending Main idle wait");
    Session::ConstructedDestination receipt;unsigned factories=0;
    Session::Services services;services.source_still_matches=[](){return true;};
    services.construct=[&](const auto& pos,const auto& current,auto*)->std::optional<Session::ConstructedDestination>{
        ++factories;require(current()&&world.create_runtime_object(20,0x7c4,{},std::vector<odh::EntryPoint>(31))&&
            world.set_position(20,pos),"pending request real factory publication");
        odh::Interaction row{};row.action=4096;row.label_index=2;
        receipt={20,world.find(20)->generation,501,installed,odh::Ttab{odh::TtabStatus::decoded,1,9,true,0,{row}}};return receipt;
    };
    Session original{world,11,vm,threads,routing};require(original.request({12,4,1,0},services),"ordinary birth command queues");
    const auto q=threads.interactions().snapshot();require(!q.active&&!routing.active()&&q.actions.size()==1,
        "birth checkpoint has its real ordinary object and queued action but no routing job");
    Session::CreatedDestinationBinding binding;binding.destination=receipt;
    binding.origin={20,receipt.generation,0x7c4,"Objects.far","Destination.iff",16807};
    binding.resource_owner.owner=501;binding.resource_owner.resource_guid=0x7c4;
    binding.resource_owner.source="Objects.far";binding.resource_owner.asset="Destination.iff";
    binding.resource_owner.definition_chunk_id=16807;binding.resource_owner.tree_table_id=128;
    binding.resource_owner.root_routine=4096;binding.resource_owner.bank_kind=odh::SessionCodeBankKind::complete_private;
    odh::World fresh;require(fresh.restore_snapshot(world.snapshot()),"birth restores its actual World without factory replay");
    odh::RouteGrid fresh_grid{32,16};odh::Routing fresh_route{fresh,fresh_grid,slots};
    odh::EntityThreadControllers fresh_threads{fresh,11};
    odh::Vm fresh_vm{main,fresh.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        return i.opcode==17?fresh_threads.interaction_primitive()(f,i,t,d):fresh_route.primitive()(f,i,t,d);},{{501,action}}};
    require(fresh_threads.bind(fresh_vm)&&fresh_route.bind(fresh_vm)&&fresh_vm.restore_snapshot(vm.snapshot())&&
        fresh_threads.restore_snapshot(threads.snapshot()),"birth restores exact queue/Main continuation into independent graph");
    auto restored_binding=binding;
    // Separate restore loaders legitimately allocate distinct immutable banks.
    // The receipt must point to the ACTOR's verified installed instance.
    restored_binding.destination.program=fresh_vm.shared_program_for_owner(501);
    const auto spatial=fresh.snapshot().spatial_revision,steps=fresh_vm.steps();
    {
        Session wrong_bank{fresh,11,fresh_vm,fresh_threads,fresh_route};
        require(!wrong_bank.restore_created_pending(binding)&&!wrong_bank.marker_id(),
            "same-content bank from another Thread cannot substitute for actual actor installed-bank receipt");
    }
    {
        auto wrong=restored_binding;++wrong.origin.generation;
        Session rejected{fresh,11,fresh_vm,fresh_threads,fresh_route};
        require(!rejected.restore_created_pending(wrong)&&!rejected.marker_id(),"birth rejects wrong creation generation");
    }
    {
        auto bad=fresh_threads.interactions().snapshot();bad.actions.front().uid=0;
        odh::InteractionQueue rejected(fresh,11);require(rejected.bind(fresh_vm)&&!rejected.restore_snapshot(bad),
            "birth's native queue boundary rejects an invalid saved command UID before helper adoption");
    }
    {
        auto bad=fresh_threads.interactions().snapshot();bad.actions.front().interaction_number=3;
        odh::EntityThreadControllers shadow{fresh,11};require(shadow.interactions().bind(fresh_vm)&&shadow.interactions().restore_snapshot(bad),
            "native birth queue can retain another semantic interaction as a valid generic action");
        Session rejected{fresh,11,fresh_vm,shadow,fresh_route};
        require(!rejected.restore_created_pending(restored_binding)&&!rejected.marker_id(),"birth cannot infer number2 from routine4096");
    }
    Session resumed{fresh,11,fresh_vm,fresh_threads,fresh_route};
    require(resumed.restore_created_pending(restored_binding)&&resumed.status()=="queued"&&resumed.marker_id()==20&&
        resumed.owner()==501&&!fresh_route.active()&&factories==1&&fresh_vm.steps()==steps&&
        fresh.snapshot().spatial_revision==spatial,"birth adoption preserves exact queued command without dispatch, factory, RNG or fake route");
    require(fresh_threads.advance(1)&&fresh_vm.run(2)==odh::VmStatus::budget_exhausted&&fresh_threads.observe()&&
        fresh_threads.interactions().active()&&!fresh_route.active()&&fresh_vm.stack().back().arguments[0]==99&&
        fresh_threads.interactions().snapshot().actions.front().call.arguments[0]==0,
        "actual pre-route BHAV mutates Param0 while queued source command retains its original explicit arguments");
    const auto active_spatial=fresh.snapshot().spatial_revision;
    const auto destination_before_adoption=*fresh.find(20);
    {
        Session active{fresh,11,fresh_vm,fresh_threads,fresh_route};
        require(active.restore_created_pending(restored_binding)&&active.marker_id()==20&&!fresh_route.active(),
            "active pre-route birth authenticates invocation and four-argument extent while preserving live parameter effects");
    }
    for(bool wrong_extent:{false,true}){
        auto bad_vm=fresh_vm.snapshot();
        if(wrong_extent)bad_vm.stack.back().arguments.push_back(7);
        else bad_vm.stack.back().invocation_ticket=1;
        odh::Vm rejected_vm{main,fresh.vm_memory(),{}, {},{{501,action}}};
        const bool admitted=rejected_vm.restore_snapshot(bad_vm);
        odh::InteractionQueue rejected(fresh,11);
        require(!admitted||(rejected.bind(rejected_vm)&&!rejected.restore_snapshot(fresh_threads.interactions().snapshot())),
            wrong_extent?"native pending adoption rejects changed active frame argument extent":
                         "native pending adoption rejects a different invocation ticket despite identical action metadata");
        require(fresh_vm.stack().back().arguments[0]==99&&fresh_vm.stack().back().arguments.size()==4&&fresh.find(20),
            "rejected native invocation cannot mutate the authenticated live graph");
    }
    require(fresh.find(20)&&fresh.find(20)->generation==destination_before_adoption.generation&&
        fresh.find(20)->position==destination_before_adoption.position&&fresh.snapshot().spatial_revision==active_spatial,
        "pending helpers never retire or mutate an ordinary Main-owned Destination after real dispatch effects");
}
void household_go_here_reuse_tests() {
    odh::World world;odh::RouteGrid grid{32,16};auto slots=slot();
    require(world.create(11,odh::EntityKind::avatar,{0,88,0})&&
            world.set_position(11,{4,4,1,0}),"resident reuse fixture creates its actor");
    odh::EntityThreadControllers threads{world,11};odh::Routing routing{world,grid,slots};
    const odh::RoutineSet main{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{17,0,0,{0,0,1,0}}},{},odh::Bhav::Metadata{0,1,0,0}}}};
    const odh::CodeOwner native_owner=500,go_here_owner=501;
    odh::Vm vm{main,world.vm_memory(),{64,4096,3},
        [&](const auto& f,const auto& i,const auto& t,const auto& d){
            if(i.opcode==17)return threads.interaction_primitive()(f,i,t,d);
            if(i.opcode==27)return routing.primitive()(f,i,t,d);
            return odh::PrimitiveOutcome{};
        },{{native_owner,main}}};
    require(threads.bind(vm)&&routing.bind(vm),"resident reuse fixture binds one VM");
    routing.set_room_pair_provider([](const auto&,const auto&)
        ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {return std::pair{1,1};});
    odh::EntityPosition destination{12,4,1,0};
    routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p){
        return frame.context.caller==11&&frame.context.code_owner==go_here_owner&&
            p==odh::RoutePoint{destination.x,destination.y}&&grid.walkable(p);
    });
    vm.start(odh::routine_key(4096),{11,11,11,0},{30000});
    require(vm.run(4)==odh::VmStatus::waiting,"resident reuse Main initially waits for input");
    const auto generation=vm.run_generation();std::uint64_t tick=0,last_uid=0;
    // Default + native + one Go Here owner fill the entire three-owner budget.
    // Many sequential FIFO completions must reuse the same immutable action.
    for(unsigned request=0;request<40;++request){
        destination={request%2?4:12,4,1,0};
        odh_host::HouseholdGoHereSession session{world,11,vm,threads,routing};
        require(request_go_here_fixture(session,world,vm,destination,go_here_owner,
                    request==0?std::optional<odh::RoutineSet>{goto_relative({10,0,254,255,10,0,5,0})}:std::nullopt),
                "repeated resident Go Here request reuses installed code after first installation");
        const auto q=threads.interactions().snapshot();
        require(q.actions.size()==1&&q.actions.front().uid>last_uid&&
                q.actions.front().call.context.code_owner==go_here_owner&&session.marker_alive(),
                "each reused request receives the next FIFO identity and its own live marker");
        const auto uid=q.actions.front().uid;const auto marker=*session.marker_id();
        for(unsigned budget=0;budget<32&&session.status()=="queued";++budget){
            ++tick;if(routing.active())routing.tick(tick);
            require(threads.advance(tick),"reused resident controllers advance on consecutive ticks");
            vm.run(8);require(threads.observe(),"reused resident action completion remains valid");
            session.observe();
        }
        require(session.marker_alive()&&world.remove(marker),
                "fixture ordinary object Main retirement follows completion, not Go Here helper cleanup");
        require(session.status()=="returned_true"&&session.route_status()=="arrived"&&
                session.owner()==go_here_owner&&session.result()==true&&
                !session.marker_alive()&&!world.find(marker)&&world.snapshot().entities.size()==1&&
                world.find(11)->position->x==destination.x&&world.find(11)->position->y==destination.y&&
                threads.interactions().size()==0&&threads.interactions().completion()&&
                threads.interactions().completion()->uid==uid&&vm.run_generation()==generation,
                "reused code completes in FIFO order, arrives and cleans every marker without VM restart");
        require(!vm.install_owner(600+request,goto_relative())&&
                vm.has_routine(odh::routine_key(4096),go_here_owner),
                "owner budget stays full while the existing Go Here root remains available");
        last_uid=uid;
    }
    const auto before=world.snapshot();const auto queue_before=threads.interactions().snapshot();
    const auto rejects_without_changes=[&](odh::Vm& target,odh::CodeOwner owner,std::uint16_t action){
        {
            odh_host::HouseholdGoHereSession rejected{world,11,target,threads,routing};
            require(!request_go_here_fixture(rejected,world,target,{20,4,1,0},owner,std::nullopt,action)&&
                    rejected.status()=="unsupported"&&!rejected.marker_id()&&!rejected.owner(),
                    "unavailable installed owner or root rejects before marker creation or adoption");
        }
        const auto after=world.snapshot();const auto q=threads.interactions().snapshot();
        require(after.entities.size()==before.entities.size()&&after.word_count==before.word_count&&
                after.generation_sequence==before.generation_sequence&&
                after.spatial_revision==before.spatial_revision&&
                world.find(11)->position==before.entities.front().position&&
                q.sequence==queue_before.sequence&&q.actions.size()==queue_before.actions.size()&&
                vm.run_generation()==generation,
                "rejected installed request and destructor leave World and resident FIFO unchanged");
    };
    rejects_without_changes(vm,999,4096);
    rejects_without_changes(vm,go_here_owner,4097);
    rejects_without_changes(vm,0,4096);
    odh::Vm other_vm{main,world.vm_memory()};
    require(!other_vm.has_routine(odh::routine_key(4096),go_here_owner),
            "a code owner installed in one VM is absent from another VM");
    rejects_without_changes(other_vm,go_here_owner,4096);
}
void household_room_portal_isolation_tests() {
    odh::World world;auto slots=slot();
    odh::RouteGrid first_grid{64,32},second_grid{64,32};
    odh_host::RoomPortalSession first_state,second_state;
    const odh::PreRankedSlotGoalProvider local_goals=[](const auto&,auto,const auto&,const auto& target){
        const odh::RoutePoint point{20,target.y};
        const auto flags=odh::reference_slot_entry_flags({target.x,target.y},target.direction,point,0x55);
        return std::vector<odh::PreRankedSlotGoal>{{point,flags,2}};
    };
    odh::Routing first{world,first_grid,slots,local_goals},second{world,second_grid,slots,local_goals};
    odh::Vm first_vm{routines(),world.vm_memory(),{},first.primitive()};
    odh::Vm second_vm{routines(),world.vm_memory(),{},second.primitive()};
    world.create(11,odh::EntityKind::avatar,{0,88,0});world.create(12,odh::EntityKind::object,{0,0,1});
    world.create(21,odh::EntityKind::avatar,{0,88,0});world.create(22,odh::EntityKind::object,{0,0,1});
    world.set_position(11,{4,4,1,0});world.set_position(12,{28,4,1,0});
    world.set_position(21,{4,20,1,0});world.set_position(22,{28,20,1,0});
    const auto owner_key=std::make_tuple(std::string("synthetic"),std::string("door.iff"),std::uint16_t{4097});
    const odh::RoutineSet portal{{odh::routine_key(4097),odh::Bhav{0x8002,
        {{2,254,255,{0,0,0,0,0,2,7,7}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    const auto configure=[&](odh::Routing& route,odh::Vm& vm,odh_host::RoomPortalSession& state,
        std::int16_t actor,std::int16_t target,odh::CodeOwner owner) {
        require(route.bind(vm),"household portal fixture binds each resident's own VM");
        route.set_route_goal_validator([actor](const odh::VmFrame& frame,odh::RoutePoint goal){
            return frame.context.caller==actor&&goal.x>=16;
        });
        route.set_room_pair_provider([](const auto& from,const auto& to)
            ->std::optional<std::pair<std::uint16_t,std::uint16_t>> {
            return std::pair{std::uint16_t(from.x<16?2:1),std::uint16_t(to.x<16?2:1)};
        });
        route.set_route_grid_refresh([](const auto&){return true;});
        route.set_room_portal_context_end_provider([state=&state](auto ticket){state->contexts.erase(ticket);});
        route.set_room_portal_step_provider([&,vm=&vm,state=&state,actor,target,owner](auto ticket,
            const auto&,const auto&,const std::optional<odh::VmCall>& previous,
            const std::optional<bool>& result,auto step){
            state->prune(*vm);
            if(!previous){
                require(step==0&&state->contexts.emplace(ticket,odh_host::RoomPortalContext{}).second,
                    "each resident accepts its independently numbered root portal context");
                require(vm->install_owner(owner,portal),"portal code installs at the resident VM safe point");
                state->code_owners.emplace(owner_key,owner);
                const odh::VmCall call{odh::routine_key(4097),{actor,target,target,owner},{}};
                state->contexts.at(ticket).pending_call=call;
                return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,call,{}};
            }
            require(step==1&&result==true&&state->contexts.count(ticket)==1&&
                previous->context.caller==actor&&previous->context.stack_object==target&&
                previous->context.code_owner==state->code_owners.at(owner_key),
                "returned portal retains its resident, ticket, object and resource owner");
            ++state->returns;
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::destination_reached,{},{}};
        });
    };
    configure(first,first_vm,first_state,11,12,501);
    configure(second,second_vm,second_state,21,22,502);
    first_vm.start(odh::routine_key(4096),{11,12,12});
    second_vm.start(odh::routine_key(4096),{21,22,22});
    require(first_vm.run(2)==odh::VmStatus::waiting&&second_vm.run(2)==odh::VmStatus::waiting&&
        first_vm.pending()->ticket==second_vm.pending()->ticket,
        "independent resident VMs deliberately reuse the same numeric route ticket");
    const auto ticket=first_vm.pending()->ticket;
    require(first.tick(1)&&second.tick(1)&&first.external_frame_active()&&second.external_frame_active(),
        "both resident Route45 jobs schedule Portal15 on their own VM");
    first_state.contexts.at(ticket).ignored.push_back({12,1});
    first_state.prune(first_vm);second_state.prune(second_vm);
    require(first_state.contexts.count(ticket)==1&&second_state.contexts.count(ticket)==1&&
        second_state.contexts.at(ticket).ignored.empty()&&
        first_state.code_owners.at(owner_key)==501&&second_state.code_owners.at(owner_key)==502,
        "same ticket and same door resource keep independent ignored edges and code-owner maps");
    world.set_position(11,{16,4,1,0});world.set_position(21,{16,20,1,0});
    require(first_vm.run(1)==odh::VmStatus::yielded&&first.tick(2)&&first_state.contexts.empty()&&
        second_state.contexts.count(ticket)==1&&second.external_frame_active(),
        "one resident's portal return cleans only its own planner context");
    require(second_vm.run(1)==odh::VmStatus::yielded&&second.tick(2)&&second_state.contexts.empty(),
        "second resident resumes its own local SLOT route after its portal return");
    for(std::uint64_t tick=3;tick<=16&&(first.active()||second.active());++tick){first.tick(tick);second.tick(tick);}
    require(first_vm.status()==odh::VmStatus::returned_true&&second_vm.status()==odh::VmStatus::returned_true&&
        world.find(11)->position->x==20&&world.find(11)->position->y==4&&
        world.find(21)->position->x==20&&world.find(21)->position->y==20&&
        first_state.returns==1&&second_state.returns==1,
        ("interleaved cross-room Route45 routes finish under separate resident VMs: first="+
            std::to_string(static_cast<int>(first_vm.status()))+","+first.diagnostic()+",x="+
            std::to_string(world.find(11)->position->x)+"; second="+
            std::to_string(static_cast<int>(second_vm.status()))+","+second.diagnostic()+",x="+
            std::to_string(world.find(21)->position->x)).c_str());
    // A completed VM cannot keep stale portal state alive.
    first_state.contexts.emplace(ticket,odh_host::RoomPortalContext{});
    first_state.prune(first_vm);
    require(first_state.contexts.empty(),"completed resident VM prunes stale portal tickets");
}
void deferred_room_route_tests() {
    {
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();
        std::size_t local_goal_calls=0,step_calls=0,refreshes=0,context_ends=0;
        std::size_t route_entries=0;
        std::uint8_t local_goal_flags=0;
        odh::PreRankedSlotGoalProvider goals=[&](const auto&,std::size_t,const auto& actor,const auto& target) {
            ++local_goal_calls;
            require(actor.x==24&&target.x==28,"local SLOT planning starts only after the actor reaches the destination room");
            const odh::RoutePoint point{20,12};
            local_goal_flags=odh::reference_slot_entry_flags({target.x,target.y},target.direction,point,0x55);
            return std::vector<odh::PreRankedSlotGoal>{{point,local_goal_flags,6}};
        };
        odh::Routing routing{world,grid,slots,goals};
        odh::SourceSparseServices prefix_services;
        prefix_services.source_tick=[&](const odh::SourceSparseMotionContext& context){
            ++route_entries;return context.root_route&&context.actor==1&&context.frame.context.caller==1&&
                context.vm&&context.vm->pending()&&context.vm->pending()->ticket==context.dispatch.ticket;
        };
        routing.set_source_sparse_services(std::move(prefix_services));
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
        std::string destination_reason="old failure";
        require(routing.intersects_destination({28,12},{},&destination_reason)==false&&destination_reason.empty(),
                "deferred SLOT route has source CurRoute null before any goal is selected");
        const auto root_ticket=vm.pending()->ticket;
        std::string stack_rejection;
        const auto deferred_stack=routing.capture_session_route_stack(0,&stack_rejection);
        if(!deferred_stack)throw std::runtime_error("deferred route capture: "+stack_rejection);
        require(deferred_stack->jobs.size()==1&&deferred_stack->jobs.front().deferred_room_route&&
                deferred_stack->jobs.front().path.empty()&&deferred_stack->jobs.front().replay_frame&&
                deferred_stack->jobs.front().room_context_open&&local_goal_calls==0&&step_calls==0&&route_entries==0,
                "deferred snapshot freezes original request before portal or SLOT callbacks");
        require(routing.tick(1)&&routing.external_frame_active()&&step_calls==1&&route_entries==1&&
                vm.pending()->ticket==root_ticket,"first Portal15 is scheduled at the original ticket safe point");
        require(routing.intersects_destination({28,12})==false,
                "external portal BHAV does not fabricate a destination for an unresolved SLOT parent");
        require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(2)&&routing.external_frame_active()&&
                step_calls==2&&refreshes==1&&route_entries==2&&vm.pending()->ticket==root_ticket,
                "false Portal15 refreshes the grid and schedules an alternate edge under the same ticket");
        world.set_position(1,{16,12,1,0});
        require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(3)&&routing.external_frame_active()&&
                step_calls==3&&refreshes==2&&route_entries==3&&vm.pending()->ticket==root_ticket,
                "successful alternate opens the next room transition without replacing the root ticket");
        world.set_position(1,{24,12,1,0});
        require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(4)&&!routing.external_frame_active()&&
                local_goal_calls==1&&context_ends==1&&route_entries==4&&vm.status()==odh::VmStatus::waiting,
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
        std::vector<odh::RouteOutcomeReceipt> terminal_receipts;
        routing.set_outcome_observer([&](const auto& receipt){terminal_receipts.push_back(receipt);});
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
        require(routing.intersects_destination({28,12})==false,
                "active and suspended pre-selection SLOT frames both have no current destination");
        const auto child_ticket=vm.pending()->ticket;
        odh_host::RoomPortalSession nested_state;
        nested_state.contexts.emplace(root_ticket,odh_host::RoomPortalContext{});
        nested_state.contexts.emplace(child_ticket,odh_host::RoomPortalContext{});
        nested_state.prune(vm);
        require(nested_state.contexts.size()==2,
                "resident portal pruning preserves suspended parent and pending nested tickets");
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
        require(terminal_receipts.size()==4&&terminal_receipts[0].stage==odh::RouteOutcomeStage::failure_decision&&
            terminal_receipts[1].completion_accepted&&terminal_receipts[1].ticket==child_ticket&&terminal_receipts[1].nested_depth==1&&
            terminal_receipts[1].source_is_pending&&terminal_receipts[1].routine.id==4097&&terminal_receipts[1].owner==9&&
            terminal_receipts[2].stage==odh::RouteOutcomeStage::failure_decision&&terminal_receipts[3].completion_accepted&&
            terminal_receipts[3].ticket==root_ticket&&terminal_receipts[3].nested_depth==0&&terminal_receipts[3].routine.id==4096,
            "nested failure and parent completion retain distinct original pending identities before reset/pop");

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
void general_route_tick_prefix_tests() {
    odh::World world;odh::RouteGrid grid{32,32};odh::SlotTable slots;
    odh::Routing routing{world,grid,slots};unsigned entries=0;
    odh::SourceSparseServices services;
    services.source_tick=[&](const odh::SourceSparseMotionContext& context){++entries;
        return context.actor==1&&context.root_route&&context.vm&&context.vm->pending()&&
            context.vm->pending()->ticket==context.dispatch.ticket;};
    routing.set_source_sparse_services(std::move(services));
    bool ended=false;
    routing.set_look_towards_services({
        [](const auto&)->std::optional<odh::SourceLookTowardsProfile>{return odh::SourceLookTowardsProfile{};},
        [](const auto&,auto,auto,auto,auto)->std::optional<std::uint32_t>{return 8;},
        [&](const auto&,auto)->std::optional<bool>{return ended;},
        [](const auto&,auto,auto){return true;}});
    odh::RoutineSet code{{odh::routine_key(4096),odh::Bhav{0x8002,
        {{22,254,255,{2,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
    odh::Vm vm{code,world.vm_memory(),{},routing.primitive()};
    world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object);
    world.set_position(1,{12,12,1,0});world.set_position(2,{20,12,1,0});routing.bind(vm);
    vm.start(odh::routine_key(4096),{1,1,2});
    require(vm.run(1)==odh::VmStatus::waiting&&entries==0,
            "LookTowards admission does not fabricate a parked routing tick");
    require(routing.tick(1)&&entries==1&&routing.active(),"LookTowards invokes the shared source tick prefix");
    require(!routing.tick(1)&&entries==1,"LookTowards bad scheduler cadence consumes no extra prefix");
    // LookTowards freezes a turn on the caller, not a walk to Stack Object2.
    const auto turn_actor=std::to_string(world.find(1)->generation);
    require(routing.diagnostic().find("routing scheduler cadence rejected now=1 last_tick=1")!=std::string::npos&&
            routing.diagnostic().find("actor=1/"+turn_actor)!=std::string::npos&&
            routing.diagnostic().find("target=1/"+turn_actor)!=std::string::npos,
            "rejected routing cadence names its clock and exact participants");
    const auto snapshot=routing.capture_session_route_stack(0);
    require(snapshot&&entries==1,"LookTowards continuation capture consumes no source tick");
    ended=true;
    require(routing.tick(2)&&entries==2&&!routing.active(),"LookTowards completion invokes its final source tick prefix");
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
        routing.set_sitting_slot_candidate_preflight([](const auto&,std::size_t,const auto&,const auto&) {
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
        routing.set_sitting_slot_candidate_preflight([](const auto& table,std::size_t index,
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
void source_reset_detachment_tests(){
    {
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
        odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(2)==odh::VmStatus::waiting&&routing.tick(1)&&routing.active(),
            "reset cadence fixture advances an actual live route");
        const auto old_run=vm.run_generation();
        require(routing.discard_for_source_reset()&&!routing.has_route_record(),
            "reset removes the complete previous routing service");
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run_generation()!=old_run&&vm.run(2)==odh::VmStatus::waiting&&routing.active(),
            "restarted Main creates a new route after its previous service was discarded");
        require(routing.tick(6)&&routing.active(),
            "new route starts its own cadence after ticks without a routing service");
        const auto pose=*world.find(1)->position;
        require(!routing.tick(8)&&*world.find(1)->position==pose&&
                routing.diagnostic().find("now=8 last_tick=6")!=std::string::npos,
            "reset does not authorize a skipped tick inside the replacement route");
        require(routing.tick(7),"rejected cadence does not consume the missing routing tick");
    }
    {
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
        std::vector<std::uint64_t> ended;std::uint64_t parent=0;
        routing.set_room_pair_provider([](const auto&,const auto&)->std::optional<std::pair<std::uint16_t,std::uint16_t>>{return std::pair{3,1};});
        routing.set_route_grid_refresh([](const auto&){return true;});
        routing.set_room_portal_context_end_provider([&](auto ticket){ended.push_back(ticket);});
        routing.set_room_portal_step_provider([&](auto ticket,const auto&,const auto&,const std::optional<odh::VmCall>& previous,const auto&,auto){
            if(ticket==parent&&!previous)return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::call_ready,
                odh::VmCall{odh::routine_key(4097),{1,2,2,9},{}},{}};
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"not dispatched"};
        });
        odh::RoutineSet portal{{odh::routine_key(4097),odh::Bhav{0x8002,{{45,254,255,{1,0,2,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
        odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive(),{{9,std::move(portal)}}};
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
        world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});routing.bind(vm);
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(2)==odh::VmStatus::waiting,"reset nested route parent waits");parent=vm.pending()->ticket;
        require(routing.tick(1)&&routing.external_frame_active()&&vm.run(1)==odh::VmStatus::waiting&&vm.pending()->ticket!=parent,
            "reset nested route owns distinct parent and child room contexts");
        const auto child=vm.pending()->ticket;const auto before=vm.snapshot();
        require(routing.discard_for_source_reset()&&!routing.has_route_record()&&ended==std::vector<std::uint64_t>{child,parent},
            "reset releases child and covered parent contexts exactly once");
        require(vm.pending()&&vm.pending()->ticket==child&&vm.run_generation()==before.run_generation&&vm.steps()==before.steps&&
            vm.stack().size()==before.stack.size(),"nested detach preserves VM ancestry for its actual recovery owner");
    }
    for(bool fail:{false,true}){
        odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();odh::Routing routing{world,grid,slots};
        std::size_t ended=0;bool reentry_rejected=false;
        routing.set_room_pair_provider([](const auto&,const auto&)->std::optional<std::pair<std::uint16_t,std::uint16_t>>{return std::pair{3,1};});
        routing.set_room_portal_context_end_provider([&](auto){
            ++ended;reentry_rejected=!routing.discard_for_source_reset();
            if(fail)throw std::runtime_error("host release failed");
        });
        routing.set_room_portal_step_provider([](auto,const auto&,const auto&,const auto&,const auto&,auto){
            return odh::RoutePortalStepPlan{odh::RoutePortalStepStatus::unavailable,{},"not dispatched"};
        });
        odh::Vm vm{routines(),world.vm_memory(),{},routing.primitive()};
        require(world.create(1,odh::EntityKind::avatar,{0,88,0})&&world.create(2,odh::EntityKind::object,{0,0,1})&&
            world.set_position(1,{4,12,1,0})&&world.set_position(2,{28,12,1,0})&&routing.bind(vm),"reset route real owners prepared");
        vm.start(odh::routine_key(4096),{1,2,2});
        require(vm.run(2)==odh::VmStatus::waiting&&routing.has_route_record(),"reset route owns a pending room traversal");
        require(world.set_static_footprint(1,false),"route actor marked dynamic before detach");
        const auto before=vm.snapshot();const auto ticket=vm.pending()->ticket;
        const auto detached=routing.discard_for_source_reset();
        require(detached!=fail&&ended==1&&reentry_rejected,"reset releases actual room context once and rejects reentry");
        require(vm.pending()&&vm.pending()->ticket==ticket&&vm.run_generation()==before.run_generation&&
            vm.status()==before.status&&vm.steps()==before.steps&&vm.stack().size()==before.stack.size(),
            "service detach does not complete old VM pending or change execution identity");
        if(!fail)require(!routing.has_route_record()&&world.find(1)->static_footprint&&routing.discard_for_source_reset()&&ended==1,
            "successful detach restores static footprint and repeated detach has no callbacks");
        else require(routing.has_route_record()&&!routing.discard_for_source_reset()&&ended==1,
            "partial failing host release cannot later masquerade as successful detach");
    }
}
int main() {
    try {busy_resident_go_here_admission();
        position_publication_tests();moving_source_target_tests();carried_target_publication_tests();sparse_position_publication_tests();
        outcome_receipt_tests();goal_selection_receipt_tests();
        destination_consultation_tests();
        source_relative_operand_matrix_tests();source_relative_early_false_tests();source_relative_no_failure_ordinary_tests();
        deferred_relative_destination_tests();
        reference_slot_lattice_tests();
        reference_slot_point_tests();
        reference_slot_direction_tests();
        reference_slot_circle_tests();
        pre_ranked_goal_tests();
        door_range_route_tests();
        route_frame_context_tests();
        shared_world_dual_controller_tests();
        household_resident_route_owner_tests();
        household_go_here_session_tests();
        household_go_here_stack_restore_tests();
        immediate_route_controller_restore();
        ordinary_go_here_factory_tests();ordinary_go_here_pending_restore_tests();household_go_here_reuse_tests();
        household_room_portal_isolation_tests();
        source_reset_detachment_tests();
        deferred_room_route_tests();
        general_route_tick_prefix_tests();
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
         std::string sitting_reason;
         require(routing.intersects_destination({0,0},std::int16_t{2})==true&&
                 !routing.intersects_destination({44,12},{},&sitting_reason)&&
                 sitting_reason.find("position was not retained")!=std::string::npos,
                 "same-chair identity is proven while a legacy approach endpoint does not invent semantic position");
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
         unsigned route_entries=0;odh::SourceSparseServices prefix_services;
         prefix_services.source_tick=[&](const auto& context){++route_entries;
             return context.root_route&&context.vm&&context.vm->pending()&&
                 context.vm->pending()->ticket==context.dispatch.ticket;};
         routing.set_source_sparse_services(std::move(prefix_services));
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
         require(routing.tick(1)&&route_entries==0&&!routing.tick(1)&&!routing.tick(3)&&route_entries==0,
                 "parked child BHAV and duplicate or skipped scheduler ticks consume no parent route prefix");
         require(routing.interrupt()&&routing.active()&&routing.external_frame_active()&&
                 vm.complete(child_ticket,true)&&vm.run(1)==odh::VmStatus::yielded&&
                 vm.external_frame_return()&&routing.tick(2)&&route_entries==1&&routing.active()&&
                 routing.external_frame_active()&&vm.stack().back().key.id==398&&
                 vm.stack().back().arguments==std::vector<std::int16_t>{4,0,0,0},
                 "Backspace waits for the child action to return, then starts Go Here's failure action");
         require(vm.run(1)==odh::VmStatus::yielded&&routing.tick(3)&&route_entries==2&&!routing.active()&&
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
         auto operands=std::array<std::uint8_t,8>{5,0,254,4,0,0,2,0};
         odh::Vm vm{goto_relative(operands),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{12,12,1,3});
         routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p) {
             const auto* actor=world.find(frame.context.caller);const auto* target=world.find(frame.context.stack_object);
             return actor&&target&&actor->position&&target->position&&actor->position->level==target->position->level&&
                 p==odh::RoutePoint{target->position->x,target->position->y}&&grid.contains(p)&&grid.walkable(p);
         });routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(4)==odh::VmStatus::waiting&&routing.active(),
                 "bounded GotoRelativePosition accepts OldTrapCount5 with OpposingDirection");
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
         const std::array<std::uint8_t,8> no_failure_trees{5,0,0,254,0,0,6,0};
         odh::Vm vm{goto_relative(no_failure_trees),world.vm_memory(),{},routing.primitive()};
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
         auto status=vm.run(8);
         for(std::size_t run=0;run<4&&status==odh::VmStatus::yielded;++run)status=vm.run(8);
         require(status==odh::VmStatus::waiting&&routing.active(),
                 "InFrontOf/Facing accepts source flags6 (NoFailureTrees plus unused bit4)");
         for(std::uint64_t tick=1;tick<=32&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&!routing.active(),
                 "unused flag4 and OldTrapCount5 leave real NoFailureTrees route geometry unchanged");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const std::array<std::uint8_t,8> undefined_direction{0,0,0,8,0,0,4,0};
         odh::Vm vm{goto_relative(undefined_direction),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,24,1,0});world.set_position(2,{20,24,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(2)==odh::VmStatus::fault&&!routing.active()&&
                 vm.diagnostic().find("outside the synthetic route profiles")!=std::string::npos,
                 "InFrontOf still rejects undefined direction instead of inventing source geometry");}
        {odh::World world;odh::RouteGrid grid{32,32};auto globals=slot();odh::Routing routing{world,grid,globals};
         const std::array<std::uint8_t,8> go_here{10,0,254,255,10,0,5,0};
         odh::Vm vm{goto_relative(go_here),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,2});world.set_position(2,{12,12,1,5});
         routing.set_room_pair_provider([](const auto&,const auto&){
             return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{3,3}};});
         routing.set_route_goal_validator([&](const odh::VmFrame& frame,odh::RoutePoint p) {
             const auto* actor=world.find(frame.context.caller);const auto* target=world.find(frame.context.stack_object);
             return actor&&target&&actor->position&&target->position&&
                 actor->position->level==target->position->level&&
                 p==odh::RoutePoint{target->position->x,target->position->y}&&grid.contains(p)&&grid.walkable(p);
         });routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(4)==odh::VmStatus::waiting&&routing.active(),
                 "saved Destination Go Here GotoRelativePosition schedules the exact selected point");
         std::string capture_rejection;
         const auto route_state=routing.capture_local_session_route(odh::CodeOwner{0},&capture_rejection);
         if(!route_state||!route_state->call_failure_trees)
             throw std::runtime_error("saved Go Here route snapshot retains failure behavior: "+capture_rejection);
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
                 "GotoRelativePosition without a host geometry validator cannot start a route");}
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
         std::size_t source_entries=0,resumed_entries=0;
         odh::SourceSparseServices prefix_services;
         prefix_services.source_tick=[&](const auto& context){++source_entries;
             return context.root_route==(context.dispatch.ticket==1)&&context.vm&&
                 context.vm->pending()&&context.vm->pending()->ticket==context.dispatch.ticket;};
         routing.set_source_sparse_services(std::move(prefix_services));
         odh::RoutineSet portal_code{{odh::routine_key(4097),odh::Bhav{0x8002,
             {{45,254,255,{1,0,2,0,0,0,0,0}},
              {2,254,255,{0,0,2,7,7,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}};
         odh::Vm vm{std::move(code),world.vm_memory(),{},routing.primitive(),{{9,portal_code}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});grid.block({28,12});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(1)==odh::VmStatus::waiting&&routing.active(),"parent Route45 waits before nested portal routing");
         const auto parent_ticket=vm.pending()->ticket;
         require(routing.call_external_frame({odh::routine_key(4097),{1,2,2,9},{}})&&vm.run(1)==odh::VmStatus::waiting&&
                 vm.pending()->ticket!=parent_ticket&&provider_calls==2,
                 "portal BHAV can issue a nested Route45 with its own wait ticket");
         require(routing.intersects_destination({6,12})==true&&
                 routing.intersects_destination({26,12})==true&&
                 routing.intersects_destination({40,12})==false,
                 "destination consultation includes selected suspended parent and active child goals");
         require(routing.active_frame_context()->state==odh::RouteFrameState::initial&&
                 routing.incoming_direction(0.25f)==0.25f&&routing.consume_portal_turn()&&
                 !routing.consume_portal_turn(),
                 "nested route reads and consumes PortalTurns on its suspended parent frame");
         require(routing.tick(1),"nested child advances before its checkpoint");
         std::string rejection;
         const auto stack_state=routing.capture_session_route_stack(0,&rejection);
         if(!stack_state)throw std::runtime_error("nested route stack capture: "+rejection);
         require(source_entries==1,"nested child consumes one prefix while capture consumes none");
         require(stack_state->jobs.size()==2&&stack_state->last_tick==1&&
                 stack_state->jobs.front().ticket==parent_ticket&&
                 // Source CanPortalTurn increments even when rejecting the
                 // second turn; both observations belong to the saved parent.
                 stack_state->jobs.front().frame.portal_turns==2&&
                 stack_state->jobs.back().expected.x!=stack_state->jobs.front().expected.x,
                 "nested snapshot preserves historical parent pose, consumed portal turn and child progress");
         odh::World resumed_world;require(resumed_world.restore_snapshot(world.snapshot()),
                 "nested checkpoint restores its World values");
         odh::RouteGrid resumed_grid{64,32};resumed_grid.block({28,12});
         std::size_t resumed_plans=0;
         odh::Routing resumed{resumed_world,resumed_grid,slots,
             [&](const auto&,std::size_t,const auto&,const auto&){++resumed_plans;
                 return std::vector<odh::PreRankedSlotGoal>{};}};
         odh::SourceSparseServices resumed_prefix;
         resumed_prefix.source_tick=[&](const auto& context){++resumed_entries;
             return context.root_route==(context.dispatch.ticket==1)&&context.vm&&
                 context.vm->pending()&&context.vm->pending()->ticket==context.dispatch.ticket;};
         resumed.set_source_sparse_services(std::move(resumed_prefix));
         odh::Vm resumed_vm{routines(),resumed_world.vm_memory(),{},resumed.primitive(),{{9,portal_code}}};
         require(resumed.bind(resumed_vm)&&resumed_vm.restore_snapshot(vm.snapshot()),
                 "nested checkpoint restores exact VM suspended tickets");
         auto malformed=*stack_state;malformed.jobs.front().pending.ticket++;
         require(!resumed.restore_session_route_stack(malformed,0,&rejection)&&!resumed.active()&&
                 resumed_world.find(1)->position==world.find(1)->position,
                 "mismatched suspended pending rejects atomically");
         malformed=*stack_state;malformed.jobs.front().external_frame_owner++;
         require(!resumed.restore_session_route_stack(malformed,0,&rejection)&&!resumed.active(),
                 "mismatched external boundary ownership rejects atomically");
         malformed=*stack_state;malformed.jobs.back().route_frame.locals.push_back(17);
         require(!resumed.restore_session_route_stack(malformed,0,&rejection)&&!resumed.active(),
                 "changed original child frame rejects atomically");
         require(resumed.restore_session_route_stack(*stack_state,0,&rejection)&&resumed_plans==0&&resumed_entries==0,
                 "nested restore reconstructs both controllers without SLOT ranking or callbacks");
         require(resumed.intersects_destination({6,12})==true&&resumed.intersects_destination({26,12})==true,
                 "restored route stack retains both semantic selected destinations");
         require(!resumed.tick(1)&&resumed_entries==0&&resumed_world.find(1)->position==world.find(1)->position,
                 "restored last_tick prevents a duplicate movement tick");
         for(std::uint64_t tick=1;tick<=16&&world.find(1)->position->x<20;++tick) {
             routing.tick(tick);resumed.tick(tick);
         }
         require(resumed_world.find(1)->position==world.find(1)->position&&
                 resumed_vm.status()==vm.status()&&resumed_plans==0&&source_entries==resumed_entries+1,
                 "nested continuous and resumed children finish identically without reranking");
         const auto returned_stack=routing.capture_session_route_stack(0,&rejection);
         require(returned_stack&&returned_stack->jobs.size()==1&&returned_stack->jobs.front().external_frame_active,
                 "external return safe point remains capturable after child controller is popped");
         require(world.find(1)->position->x==20&&world.find(1)->static_footprint&&routing.active()&&
                 vm.status()==odh::VmStatus::yielded&&vm.external_frame_return()&&
                 vm.external_frame_return()->ticket==parent_ticket,
                 "nested Route45 returns control to the portal BHAV without dropping its parent job");
         require(routing.tick(17)&&vm.status()==odh::VmStatus::waiting&&vm.pending()->ticket==parent_ticket&&
                 world.find(1)->position->x==20,
                 "parent Route45 revalidates and replans from the nested route destination");
         require(routing.tick(18)&&world.find(1)->position->x==19&&
                 !world.find(1)->static_footprint&&routing.active(),
                 "parent route movement resumes on a later tick with its dynamic footprint restored");
         require(resumed.tick(17)&&resumed.tick(18)&&resumed_plans==0&&
                 resumed_world.find(1)->position==world.find(1)->position&&
                 resumed.active_frame_context()->state==routing.active_frame_context()->state,
                 "restored parent resumes from child's destination with matching portal lifecycle");
         for(std::uint64_t tick=19;tick<40&&(routing.active()||resumed.active());++tick) {
             if(routing.active())routing.tick(tick);if(resumed.active())resumed.tick(tick);
         }
         require(!routing.active()&&!resumed.active()&&vm.status()==resumed_vm.status()&&
                 world.find(1)->position==resumed_world.find(1)->position&&
                 world.find(1)->person_data==resumed_world.find(1)->person_data&&resumed_plans==0&&source_entries==resumed_entries+1,
                 "entire nested route chain has identical continuous and resumed final state");}
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
                 f.vm.diagnostic().find("pre-ranked SLOT goal provider returned no candidates")==0,
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
        {odh::World world;odh::RouteGrid grid{64,64};
         odh::SlotRecord point{};point.type=3;point.flags=0;point.offset_bits={0,0xC1800000U,0};
         point.standing=1;point.sitting=0;point.ground=0;point.snap_target_slot=-1;
         point.proximity=std::array<std::int32_t,3>{16,16,16};point.max_size=0;point.unknown_i10=0;
         point.gradient_bits=0x3E400000;point.height=0;point.facing=4;point.resolution=16;
         odh::SlotTable cabinet{10,10,std::vector<odh::SlotRecord>(1,point),0};
         std::size_t ranked_provider_calls=0;
         odh::Routing routing{world,grid,cabinet,[&](const auto&,auto,const auto&,const auto&) {
             ++ranked_provider_calls;return std::vector<odh::PreRankedSlotGoal>{};}};
         routing.set_route_goal_validator([](const odh::VmFrame&,odh::RoutePoint goal){return goal==odh::RoutePoint{28,4};});
         odh::Vm vm{routines(2,0,0),world.vm_memory(),{},routing.primitive()};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,20,1,0});world.set_position(2,{28,20,1,0});routing.bind(vm);
         vm.start(odh::routine_key(4096),{1,2,2});
         require(vm.run(10)==odh::VmStatus::waiting&&routing.active()&&ranked_provider_calls==0,
                 "exact point SLOT takes precedence over a generic ranked-goal provider");
         for(std::uint64_t tick=1;tick<=64&&routing.active();++tick)routing.tick(tick);
         require(vm.status()==odh::VmStatus::returned_true&&world.find(1)->position->x==28&&
                 world.find(1)->position->y==4&&ranked_provider_calls==0,
                 "exact point SLOT reaches its verified transformed target without consulting ranked goals");}
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
        {odh::World world;odh::RouteGrid grid{64,32};auto slots=slot();std::size_t ranked_calls=0;
         auto& ranged=slots.records[1];ranged.flags=0xA0FF;
         ranged.proximity=std::array<std::int32_t,3>{16,64,16};ranged.gradient_bits=0;
         ranged.facing=-3;ranged.resolution=8;
         const auto make_goals=[&](const auto&,std::size_t,const auto&,const auto&){
             ++ranked_calls;return std::vector<odh::PreRankedSlotGoal>{{{12,12},
                 odh::reference_slot_entry_flags({28,12},0,{12,12},0xFF),2}};};
         odh::Routing routing{world,grid,slots,make_goals};
         routing.set_ranged_slot_goal_provider(make_goals);
         routing.set_room_pair_provider([](const auto&,const auto&){
             return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{3,3}};});
         routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint point){
             return grid.contains(point)&&grid.walkable(point);});
         odh::Vm vm{{},world.vm_memory(),{},routing.primitive(),{{1,routines()}}};
         world.create(1,odh::EntityKind::avatar,{0,88,0});world.create(2,odh::EntityKind::object,{0,0,1});
         world.set_position(1,{4,12,1,0});world.set_position(2,{28,12,1,0});
         routing.bind(vm);vm.start(odh::routine_key(4096),{1,2,2,1});
         require(vm.run(10)==odh::VmStatus::waiting&&routing.active()&&ranked_calls==1,
                 "local Route45 creates one already-ranked path before save");
         require(routing.tick(1)&&routing.tick(2)&&world.find(1)->position->x==6&&
                 !world.find(1)->static_footprint,"local route advances to a save-safe mid-walk boundary");
         std::string initial_capture_rejection;
         const auto route_state=routing.capture_local_session_route(std::nullopt,&initial_capture_rejection);
         if(!route_state)throw std::runtime_error("local route capture: "+initial_capture_rejection);
         require(route_state&&route_state->next==3&&route_state->path[route_state->next-1]==
                 odh::RoutePoint{6,12},"local route snapshot records the selected path and current progress");
         std::string capture_rejection;
         world.set_position(2,{29,12,1,0});
         require(!routing.capture_local_session_route(std::nullopt,&capture_rejection)&&
                 capture_rejection=="actor or destination changed since the route movement checkpoint",
                 "unsafe local route capture reports the changed destination");
         world.set_position(2,{28,12,1,0});
         const auto world_state=world.snapshot();const auto vm_state=vm.snapshot();
         odh::World resumed_world;require(resumed_world.restore_snapshot(world_state),
                 "restored route World snapshot is accepted");
         odh::RouteGrid resumed_grid{64,32};odh::Routing resumed_routing{resumed_world,resumed_grid,slots,
             [&](const auto&,std::size_t,const auto&,const auto&){++ranked_calls;
                 return std::vector<odh::PreRankedSlotGoal>{{{12,12},0x04,2}};}};
         resumed_routing.set_ranged_slot_goal_provider([&](const auto&,std::size_t,const auto&,const auto&){
             ++ranked_calls;return std::vector<odh::PreRankedSlotGoal>{{{12,12},
                 odh::reference_slot_entry_flags({28,12},0,{12,12},0xFF),2}};});
         resumed_routing.set_room_pair_provider([](const auto&,const auto&){
             return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{3,3}};});
         resumed_routing.set_route_goal_validator([&](const odh::VmFrame&,odh::RoutePoint point){
             return resumed_grid.contains(point)&&resumed_grid.walkable(point);});
         odh::Vm resumed_vm{{},resumed_world.vm_memory(),{},resumed_routing.primitive(),{{1,routines()}}};
         require(resumed_routing.bind(resumed_vm)&&resumed_vm.restore_snapshot(vm_state)&&
                 resumed_routing.restore_local_session_route(*route_state)&&ranked_calls==1,
                 "route resumes from its saved path without rerunning randomized SLOT ranking");
         odh::World blocked_world;require(blocked_world.restore_snapshot(world_state),
                 "blocked-route fixture restores the same saved World");
         odh::RouteGrid blocked_grid{64,32};blocked_grid.block(route_state->path[route_state->next]);
         odh::Routing blocked_routing{blocked_world,blocked_grid,slots};
         blocked_routing.set_room_pair_provider([](const auto&,const auto&){
             return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{3,3}};});
         odh::Vm blocked_vm{{},blocked_world.vm_memory(),{},blocked_routing.primitive(),{{1,routines()}}};
         require(blocked_routing.bind(blocked_vm)&&blocked_vm.restore_snapshot(vm_state)&&
                 !blocked_routing.restore_local_session_route(*route_state)&&!blocked_routing.active()&&
                 blocked_world.find(1)->position->x==6,
                 "restoring a route fails closed when the saved remaining path is now blocked");
         auto cross_floor_world_state=world_state;
         auto target_state=std::find_if(cross_floor_world_state.entities.begin(),
             cross_floor_world_state.entities.end(),[](const auto& entity){return entity.id==2;});
         require(target_state!=cross_floor_world_state.entities.end()&&target_state->position.has_value(),
                 "cross-floor route fixture retains its target");
         target_state->position->level=2;
         odh::World cross_floor_world;require(cross_floor_world.restore_snapshot(cross_floor_world_state),
                 "cross-floor route fixture restores its altered target level");
         odh::RouteGrid cross_floor_grid{64,32};odh::Routing cross_floor_routing{
             cross_floor_world,cross_floor_grid,slots};
         cross_floor_routing.set_room_pair_provider([](const auto&,const auto&){
             return std::optional<std::pair<std::uint16_t,std::uint16_t>>{{3,3}};});
         odh::Vm cross_floor_vm{{},cross_floor_world.vm_memory(),{},cross_floor_routing.primitive(),
             {{1,routines()}}};
         require(cross_floor_routing.bind(cross_floor_vm)&&cross_floor_vm.restore_snapshot(vm_state)&&
                 !cross_floor_routing.restore_local_session_route(*route_state),
                 "restoring a local route rejects different actor and target floors even with the same room ID");
         for(std::uint64_t tick=3;tick<=16&&(routing.active()||resumed_routing.active());++tick){
             if(routing.active())routing.tick(tick);
             if(resumed_routing.active())resumed_routing.tick(tick);
         }
         const auto* original_actor=world.find(1);const auto* resumed_actor=resumed_world.find(1);
         require(!routing.active()&&!resumed_routing.active()&&vm.status()==odh::VmStatus::returned_true&&
                 resumed_vm.status()==vm.status()&&original_actor&&resumed_actor&&
                 original_actor->position&&resumed_actor->position&&
                 *original_actor->position==*resumed_actor->position&&
                 original_actor->person_data==resumed_actor->person_data&&
                 original_actor->static_footprint&&resumed_actor->static_footprint,
                 "continued and restored local routes reach identical position and arrival state");}
        {Fixture f;require(!f.world.set_position(1,{-1,0,1,0}) && !f.world.set_position(1,{0,0,0,0}) &&
                            !f.world.set_position(1,{0,0,1,8}),"position profile bounds");}
        std::cout<<checks<<" routing checks passed (constructed world only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Routing test failed: "<<e.what()<<'\n';return 1;}
}
