#include "odh/slot_scoring.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
using Verification = odh::ReferenceSlotVerification;
odh::SlotTable table(std::uint32_t flags = 0x55) {
    odh::SlotRecord r{};
    r.type=3; r.standing=1; r.flags=flags; r.snap_target_slot=-1;
    r.proximity=std::array<std::int32_t,3>{16,16,16}; r.max_size=0; r.unknown_i10=0;
    r.gradient_bits=0x3E400000; r.height=0; r.facing=-2; r.resolution=16;
    return {0,10,{r},0};
}
odh::ReferenceSlotScoreInput input(odh::RoutePoint p, Verification v = Verification::eligible) {
    return {{p,4,2},16,v};
}
odh::SlotTable door_range_table() {
    auto result=table(0x54);
    result.records[0].proximity=std::array<std::int32_t,3>{16,32,16};
    return result;
}
odh::SlotTable house07_global_range_table() {
    auto result=table(0xA0FF);
    auto& slot=result.records[0];
    slot.proximity=std::array<std::int32_t,3>{16,64,16};
    slot.gradient_bits=0;slot.facing=-3;slot.resolution=8;
    return result;
}
odh::SlotTable child_shy_sitting_table() {
    std::vector<odh::SlotRecord> records(11);
    for(auto& slot:records)slot.type=3;
    auto& slot=records[10];
    slot.flags=0x8055;slot.offset_bits={0,0,0};slot.proximity=std::array<std::int32_t,3>{32,80,32};
    slot.standing=1;slot.sitting=10;slot.ground=0;slot.snap_target_slot=-1;
    slot.max_size=0;slot.unknown_i10=0;slot.gradient_bits=0x3E400000;slot.height=0;
    slot.facing=-2;slot.resolution=16;
    return {0,10,std::move(records),0};
}
template<class F> void invalid_unchanged(odh::RandomState& random, F operation) {
    const auto before=random.seed();
    bool rejected=false;
    try { operation(); } catch (const std::invalid_argument&) { rejected=true; }
    catch (const std::length_error&) { rejected=true; }
    require(rejected,"malformed input rejected");
    require(random.seed()==before,"malformed input preserves RNG");
}
void formulas() {
    odh::RandomState random(1), expected(1);
    const auto first = expected.next(1024);
    expected.next(1024);
    const auto third = expected.next(1024);
    // object distance floor(sqrt(2^2+3^2))=3. maxScore=(3+16)/3+2=8.
    // candidate distance floor(sqrt(1^2+2^2))=2, subtract 2/3.0.
    const auto result=odh::score_reference_slot_candidates(table(),0,{2,3},{0,0},
        {input({1,2}),input({5,5},Verification::rejected_after_score),
         input({1,2},Verification::eligible_with_penalty)},random);
    require(result.random_calls==3,"reject after score consumes one random draw");
    require(result.seed_before==1 && result.seed_after==expected.seed() && random.seed()==expected.seed(),"explicit seed snapshot");
    require(result.records.size()==3 && result.ranked_goals.size()==2,"reject retained only in diagnostic records");
    require(result.records[0].base_score==static_cast<float>(first)/1024.0f+8,"normal base and integer maxScore");
    require(*result.records[0].final_score==result.records[0].base_score-2/3.0,"1/16 distance truncates before fractional divide");
    require(!result.records[1].final_score,"rejected candidate has no final score");
    require(result.records[1].verification==Verification::rejected_after_score,"rejection classification preserved");
    require(*result.records[2].final_score==(static_cast<float>(third)/1024.0f+8-2/3.0)/100000,"known avatar door or claim penalty once");
    require(result.records[0].seed_before==1 && result.records[0].seed_after==result.records[1].seed_before &&
        result.records[1].seed_after==result.records[2].seed_before,"per-record RNG chain");
    require(result.ranked_input_indices==std::vector<std::size_t>{0,2},"descending accepted scores");
    require(result.ranked_goals[0].position==odh::RoutePoint{1,2} && result.ranked_goals[0].facing==2 &&
        result.ranked_goals[0].entry_flags==4,"ranked output carries runtime goal metadata");
}
void branches() {
    odh::RandomState random(1), expected(1);
    const double draw=static_cast<float>(expected.next(1024))/1024.0f;
    auto result=odh::score_reference_slot_candidates(table(0x55|8192),0,{2,2},{0,0},{input({16,16})},random);
    require(result.records[0].base_score==draw && *result.records[0].final_score==draw,"random scoring omits proximity and caller distance");
    require(result.random_calls==1 && random.seed()==expected.seed(),"random scoring consumes RNG");
    random.reset(99);
    result=odh::score_reference_slot_candidates(table(0x55|(1U<<29)),0,{2,2},{0,0},
        {input({1,2}),input({3,3},Verification::rejected_after_score)},random);
    require(result.random_calls==0 && random.seed()==99 && result.seed_after==99,"equal proximity consumes no RNG even rejects");
    require(result.records[0].base_score==8 && *result.records[0].final_score==8-2/3.0,"equal proximity skips proximity bonus then subtracts caller distance");
    result=odh::score_reference_slot_candidates(table(0x55|8192|(1U<<29)),0,{2,2},{0,0},
        {input({17,16}),input({1,2}),input({1,1},Verification::eligible_with_penalty)},random);
    require(result.random_calls==0 && random.seed()==99,"equal priority supersedes random draw");
    require(*result.records[0].final_score==8 && *result.records[1].final_score==8,"combined flags omit distance");
    require(result.ranked_input_indices==std::vector<std::size_t>{0,1,2},"equal score sort preserves enumeration order");
    require(*result.records[2].final_score==8.0/100000,"equal mode penalty");
    random.reset(0);
    result=odh::score_reference_slot_candidates(table(),0,{0,0},{0,0},{input({16,0}),input({0,16})},random);
    require(result.random_calls==2 && random.seed()==0,"zero is explicit valid absorbing seed");
    require(result.ranked_input_indices==std::vector<std::size_t>{0,1},"normal-score ties stable");
    result=odh::score_reference_slot_candidates(table(),0,{0,0},{0,0},
        {input({16,0}),input({0,0}),input({1,0})},random);
    require(result.ranked_input_indices==std::vector<std::size_t>{1,2,0},"descending rank uses score rather than input or route cost");
    result=odh::score_reference_slot_candidates(table(),0,{0,0},{0,0},
        {input({16,0},Verification::rejected_after_score)},random);
    require(result.ranked_goals.empty() && result.random_calls==1 && result.records.size()==1,"all rejected still consumes scoring draws");
    result=odh::score_reference_slot_candidates(table(),0,{0,0},{0,0},{},random);
    require(result.records.empty() && result.ranked_goals.empty() && result.random_calls==0,"empty prefiltered list valid without draw");
}
void door_range() {
    odh::RandomState random(1), expected(1);
    const auto draw0=static_cast<float>(expected.next(1024))/1024.0f;
    expected.next(1024); // rejected after scoring still consumes the draw
    const auto draw2=static_cast<float>(expected.next(1024))/1024.0f;
    const std::vector<odh::ReferenceSlotScoreInput> inputs{
        {{{116,100},4,2},16,Verification::eligible},
        {{{100,116},16,2},16,Verification::rejected_after_score},
        {{{132,100},4,2},32,Verification::eligible_with_penalty}
    };
    const auto result=odh::score_reference_slot_candidates(door_range_table(),0,
        {100,100},{0,100},inputs,random);
    // maxScore = max(16-16,32-16) + (100+32)/3 + 2 = 62.
    require(result.random_calls==3 && random.seed()==expected.seed(),
        "Door ranged SLOT preserves Simitone enumeration RNG consumption");
    require(result.records[0].base_score==draw0+62 &&
        *result.records[0].final_score==draw0+62-116/3.0,
        "Door range score rewards the optimal 16-unit proximity and subtracts caller distance");
    require(!result.records[1].final_score && result.records[1].seed_after!=result.records[1].seed_before,
        "Door range rejection after scoring retains consumed RNG but not a goal");
    require(*result.records[2].final_score==(draw2+62-16-132/3.0)/100000,
        "Door range maximum proximity and known occupancy penalty match the reference formula");
    require(result.ranked_input_indices==std::vector<std::size_t>{0,2},
        "Door range returns accepted candidates in descending score order");
    auto bad=inputs;bad[0].slot_distance=33;
    invalid_unchanged(random,[&]{(void)odh::score_reference_slot_candidates(door_range_table(),0,
        {100,100},{0,100},bad,random);});
    bad=inputs;bad[0].goal.entry_flags=0x02;
    invalid_unchanged(random,[&]{(void)odh::score_reference_slot_candidates(door_range_table(),0,
        {100,100},{0,100},bad,random);});
}
void door_range_generation() {
    odh::RandomState random(0);
    const odh::EntityPosition caller{0,0,1,0},target{0,0,1,0};
    const auto goals=odh::rank_reference_range_slot(door_range_table(),0,caller,target,random);
    require(!goals.empty()&&goals.size()<=25,"Door range provider bounds candidates to its 5x5 Simitone lattice");
    require(goals.size()>=3&&goals[0].position==odh::RoutePoint{-16,0}&&
            goals[1].position==odh::RoutePoint{0,16}&&goals[2].position==odh::RoutePoint{16,0},
            "Door range provider preserves x-outer/y-inner order for equal-score optimal goals");
    for(const auto& goal:goals) {
        const auto dx=goal.position.x-target.x,dy=goal.position.y-target.y;
        const auto distance=std::sqrt(static_cast<double>(dx*dx+dy*dy));
        require(distance>=15.5&&distance<=32.5&&goal.entry_flags!=0&&
                (goal.entry_flags&static_cast<std::uint8_t>(~0x54U))==0&&goal.facing<8,
                "Door provider emits only in-band, direction-masked and faced goals");
    }
    require(random.seed()==0,"explicit zero seed remains an absorbing RNG state during range generation");
    auto alternate_mask=door_range_table();alternate_mask.records[0].flags=0x45;
    odh::RandomState alternate_random(0);
    const auto alternate_goals=odh::rank_reference_range_slot(
        alternate_mask,0,caller,target,alternate_random);
    require(!alternate_goals.empty(),"mirrored Door SLOT128 directional mask keeps its verified range profile");
    for(const auto& goal:alternate_goals)
        require(goal.entry_flags!=0&&(goal.entry_flags&static_cast<std::uint8_t>(~0x45U))==0,
            "mirrored Door range goals respect the authored directional mask");
    auto malformed=door_range_table();malformed.records[0].facing=-3;
    const auto before=random.seed();bool rejected=false;
    try{(void)odh::rank_reference_range_slot(malformed,0,caller,target,random);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&random.seed()==before,"unsupported facing profile is rejected without consuming route RNG");
}
void bed_north_circle_generation() {
    const odh::EntityPosition caller{0,64,1,0},target{64,64,1,0};
    constexpr std::array<odh::RoutePoint,8> headings={{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    for(const auto flags:{0x01U,0x10U}) {
        const auto bed=table(flags);
        const auto direction=static_cast<std::uint8_t>(flags==0x01U?0:2);
        const auto candidates=odh::enumerate_reference_slot_circle_candidates(bed,0,{64,64},direction);
        const auto allowed=std::count_if(candidates.begin(),candidates.end(),
            [](const auto& candidate){return candidate.entry_flags!=0;});
        require(allowed>0,"Beds.iff radius-16 SLOT exposes its authored sector");
        odh::RandomState random(7);
        auto oriented_target=target;oriented_target.direction=direction;
        const auto goals=odh::rank_reference_range_slot(bed,0,caller,oriented_target,random);
        require(goals.size()==allowed,"bed circle ranks all sector destinations");
        for(const auto& goal:goals) {
            const odh::RoutePoint delta{(target.x>goal.position.x)-(target.x<goal.position.x),
                (target.y>goal.position.y)-(target.y<goal.position.y)};
            const auto expected=std::find(headings.begin(),headings.end(),delta);
            require(goal.entry_flags==flags&&expected!=headings.end()&&
                    goal.facing==static_cast<std::uint8_t>(expected-headings.begin()),
                "bed destination stays in its authored sector and faces the bed");
        }
    }
}
void in_front_relative_generation() {
    odh::SlotRecord relative{};
    relative.type=3;relative.flags=0x01;relative.offset_bits={0,0,0};
    relative.standing=1;relative.sitting=0;relative.ground=0;relative.snap_target_slot=-1;
    relative.proximity=std::array<std::int32_t,3>{16,24,16};relative.max_size=100;
    relative.unknown_i10=0;relative.gradient_bits=0;relative.height=0;
    relative.facing=-2;relative.resolution=16;
    const odh::SlotTable table{0,10,{relative},0};
    odh::RandomState random(7);
    const odh::EntityPosition caller{48,80,1,0},target{80,80,1,0};
    const auto goals=odh::rank_reference_range_slot(table,0,caller,target,random);
    require(goals.size()==1&&goals.front().position==odh::RoutePoint{80,64},
        "InFrontOf/Facing relative routing selects the exact north-facing lattice point");
    for(const auto& goal:goals) {
        const auto dx=goal.position.x-target.x,dy=goal.position.y-target.y;
        const auto distance=std::sqrt(static_cast<double>(dx*dx+dy*dy));
        require(dy<0&&distance>=15.5&&distance<=24.5&&goal.entry_flags==0x01&&goal.facing<8,
            "relative sit approach stays in the front 16-to-24 band with target-facing direction");
    }
    auto malformed=table;malformed.records[0].max_size=0;
    const auto before=random.seed();bool rejected=false;
    try{(void)odh::rank_reference_range_slot(malformed,0,caller,target,random);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&random.seed()==before,
        "relative range recognizer requires the VM's transient SLOT defaults");
    const auto before_room_reject=random.seed();
    const auto outside_room=odh::rank_reference_range_slot(table,0,caller,target,random,false,
        [](odh::RoutePoint){return false;});
    require(outside_room.empty()&&random.seed()==before_room_reject,
        "room rejection happens before relative SLOT scoring consumes RNG");
    odh::RandomState expected_after_obstacle(before_room_reject);
    expected_after_obstacle.next(1024);
    const auto blocked=odh::rank_reference_range_slot(table,0,caller,target,random,false,
        [](odh::RoutePoint){return true;},[](odh::RoutePoint){return false;});
    require(blocked.empty()&&random.seed()==expected_after_obstacle.seed(),
        "position rejection happens after relative SLOT scoring consumes RNG");
}
void house07_global_range() {
    const auto table=house07_global_range_table();
    odh::RandomState random(7),expected(7);
    const auto draw0=static_cast<float>(expected.next(1024))/1024.0f;
    expected.next(1024); // rejected after candidate scoring still consumes its draw
    const auto draw2=static_cast<float>(expected.next(1024))/1024.0f;
    const std::vector<odh::ReferenceSlotScoreInput> inputs{
        {{{116,100},4,0},16,Verification::eligible},
        {{{100,116},16,0},64,Verification::rejected_after_score},
        {{{124,100},4,0},24,Verification::eligible_with_penalty}
    };
    const auto scores=odh::score_reference_slot_candidates(table,0,{100,100},{0,100},inputs,random);
    require(scores.random_calls==3&&random.seed()==expected.seed(),
        "House07 Global SLOT8 randomized scoring consumes one draw per matching direction candidate");
    require(scores.records[0].base_score==draw0&&*scores.records[0].final_score==draw0&&
            !scores.records[1].final_score&&
            *scores.records[2].final_score==static_cast<double>(draw2)/100000,
        "RandomScoring omits proximity and caller-distance terms while retaining occupancy penalty");

    odh::RandomState route_random(0);
    const odh::EntityPosition caller{16,80,1,3},target{80,80,1,0};
    const auto goals=odh::rank_reference_range_slot(table,0,caller,target,route_random);
    require(goals.size()==204&&route_random.seed()==0,
        "House07 Global SLOT8 ranks all 204 source-enumerated candidates under explicit RNG");
    for(const auto& goal:goals)
        require(goal.entry_flags!=0&&(goal.entry_flags&0xFFU)==goal.entry_flags&&goal.facing==0,
            "FaceAnywhere ranged goals retain their direction mask and neutral facing value");

    auto unsupported=table;unsupported.records[0].flags|=1U<<30;
    const auto seed_before=route_random.seed();bool rejected=false;
    try{(void)odh::rank_reference_range_slot(unsupported,0,caller,target,route_random);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&route_random.seed()==seed_before,
        "unverified square-distance variant remains rejected without consuming RNG");
}
void child_shy_sitting_fallback() {
    const auto slots=child_shy_sitting_table();
    require(odh::is_child_shy_sitting_fallback_slot(slots,10),
        "only the exact Legacy child-shy SLOT100 type-3 index10 record is recognized");
    require(!odh::is_child_shy_sitting_fallback_slot(slots,9),
        "the child-shy profile is bound to type-3 index10");
    const odh::EntityPosition caller{0,0,1,0},target{100,100,1,0};
    odh::RandomState random(7);
    const auto before=random.seed();bool rejected=false;
    try{(void)odh::rank_reference_range_slot(slots,10,caller,target,random);}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&random.seed()==before,
        "Sitting=10 is never bypassed without a host proof that no sitting target exists");
    const auto goals=odh::rank_reference_range_slot(slots,10,caller,target,random,true);
    require(!goals.empty(),"verified no-chair fallback enumerates standing goals");
    for(const auto& goal:goals){
        const auto dx=goal.position.x-target.x,dy=goal.position.y-target.y;
        const auto d2=static_cast<std::int64_t>(dx)*dx+static_cast<std::int64_t>(dy)*dy;
        require(d2>=993&&d2<=6480&&goal.entry_flags!=0,
            "standing fallback goals remain inside the authored 32-to-80 proximity band");
    }
    const auto blocked_position=goals.front().position;
    odh::RandomState sitting_random(7);
    const auto sitting_goals=odh::rank_reference_range_slot(slots,10,caller,target,sitting_random,false,{},
        [blocked_position](odh::RoutePoint point){return !(point==blocked_position);},
        [blocked_position](odh::RoutePoint point,std::uint8_t)->std::optional<std::int16_t>{
            return point==blocked_position?std::optional<std::int16_t>{42}:std::nullopt;
        },
        [blocked_position](odh::RoutePoint)->std::optional<odh::RoutePoint>{
            return odh::RoutePoint{blocked_position.x-1,blocked_position.y};
        });
    const auto selected_seat=std::find_if(sitting_goals.begin(),sitting_goals.end(),
        [](const auto& goal){return goal.sitting_object==42;});
    require(selected_seat!=sitting_goals.end(),
        "candidate resolver can retain one validated chair destination among standing fallback goals");
    require(selected_seat->sitting_approach&&
            *selected_seat->sitting_approach==odh::RoutePoint{blocked_position.x-1,blocked_position.y},
        "seat destination retains a separately reachable route endpoint for EntryPoint26");
    const auto dx=selected_seat->position.x-target.x,dy=selected_seat->position.y-target.y;
    const double seat_distance=std::sqrt(static_cast<double>(dx*dx+dy*dy));
    const odh::PreRankedSlotGoal standing_goal{
        selected_seat->position,selected_seat->entry_flags,selected_seat->facing};
    const odh::PreRankedSlotGoal sitting_goal{
        selected_seat->position,selected_seat->entry_flags,selected_seat->facing,42};
    const std::vector<odh::ReferenceSlotScoreInput> seat_weight_inputs{
        {standing_goal,seat_distance,odh::ReferenceSlotVerification::eligible},
        {sitting_goal,seat_distance,odh::ReferenceSlotVerification::eligible}
    };
    odh::RandomState seat_weight_random(0);
    const auto seat_weight_scores=odh::score_reference_slot_candidates(
        slots,10,{target.x,target.y},{caller.x,caller.y},seat_weight_inputs,seat_weight_random,false,true);
    require(seat_weight_scores.records[0].final_score&&seat_weight_scores.records[1].final_score&&
            *seat_weight_scores.records[1].final_score==10*(*seat_weight_scores.records[0].final_score),
        "Sitting=10 multiplies only a destination carrying a validated chair ID");
    for(const auto mutate:{0,1,2,3}){
        auto altered=child_shy_sitting_table();auto& slot=altered.records[10];
        if(mutate==0)slot.flags^=1;
        if(mutate==1)slot.sitting=0;
        if(mutate==2)slot.proximity=std::array<std::int32_t,3>{32,80,31};
        if(mutate==3)slot.resolution=8;
        require(!odh::is_child_shy_sitting_fallback_slot(altered,10),
            "changed flags, sitting weight, proximity, or resolution fail the exact-profile gate");
    }

    odh::World no_chair_world;
    no_chair_world.create(1,odh::EntityKind::avatar);
    no_chair_world.create(2,odh::EntityKind::object);
    require(no_chair_world.define_type(2,1),"known empty-table object identity binds");
    require(no_chair_world.define_entry_points(2,std::vector<odh::EntryPoint>(27)),
        "known EntryPoint table without actions binds");
    no_chair_world.set_position(2,{10,10,2,0});
    require(!odh::child_shy_sitting_candidate_inventory_preflight(no_chair_world,slots,10,{100,100,1,0}),
        "objects on another floor do not become sitting targets");
    no_chair_world.set_position(2,{100,140,1,0});
    require(!odh::child_shy_sitting_candidate_inventory_preflight(no_chair_world,slots,10,{100,100,1,0}),
        "a known table without an active EntryPoint26 permits the standing fallback");
    odh::World unknown_world;
    unknown_world.create(2,odh::EntityKind::object);
    unknown_world.set_position(2,{132,100,1,0});
    auto unknown=odh::child_shy_sitting_candidate_inventory_preflight(unknown_world,slots,10,{100,100,1,0});
    require(unknown&&unknown->find("EntryPoint26 table")!=std::string::npos,
        "a nearby object without a known entry table keeps the sitting choice unsupported");
    odh::World chair_world;
    chair_world.create(2,odh::EntityKind::object);
    require(chair_world.define_type(2,2),"chair object identity binds");
    chair_world.set_position(2,{132,100,1,0});
    std::vector<odh::EntryPoint> chair_entries(27);
    chair_entries[26].action=4096;
    require(chair_world.define_entry_points(2,chair_entries),"test chair EntryPoint table binds");
    auto chair=odh::child_shy_sitting_candidate_inventory_preflight(chair_world,slots,10,{100,100,1,0});
    require(!chair,
        "a known EntryPoint26 is left for exact per-candidate footprint selection");
    odh::World small_action_world;
    small_action_world.create(2,odh::EntityKind::object);
    small_action_world.define_type(2,2);
    small_action_world.set_position(2,{132,100,1,0});
    chair_entries[26].action=1;
    require(small_action_world.define_entry_points(2,chair_entries),"small EntryPoint26 action binds");
    chair=odh::child_shy_sitting_candidate_inventory_preflight(small_action_world,slots,10,{100,100,1,0});
    require(!chair,
        "any non-zero EntryPoint26 action is retained for per-candidate resolution");
    chair_world.set_position(2,{181,100,1,0});
    chair=odh::child_shy_sitting_candidate_inventory_preflight(chair_world,slots,10,{100,100,1,0});
    require(!chair,
        "a distant seat is accepted by inventory preflight and checked by its footprint per candidate");
    odh::World unpositioned_world;
    unpositioned_world.create(2,odh::EntityKind::object);
    require(!odh::child_shy_sitting_candidate_inventory_preflight(
        unpositioned_world,slots,10,{100,100,1,0}),
        "an out-of-world object without a position is excluded like Simitone's scan");
    require(odh::child_shy_sitting_candidate_inventory_preflight(
        chair_world,house07_global_range_table(),10,{100,100,1,0}).has_value(),
        "chair preflight refuses to classify an unrelated SLOT profile");
}
void invalid_inputs() {
    odh::RandomState random(42);
    auto invoke=[&](const odh::SlotTable& slots,const std::vector<odh::ReferenceSlotScoreInput>& inputs,
                    odh::RoutePoint object=odh::RoutePoint{0,0}) {
        return odh::score_reference_slot_candidates(slots,0,object,{0,0},inputs,random);
    };
    for (const auto v : {Verification::unknown,static_cast<Verification>(255)})
        invalid_unchanged(random,[&]{invoke(table(),{input({16,0}),input({0,16},v)});});
    for (const auto value : {std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-1.0,15.5}) {
        auto bad=input({16,0}); bad.slot_distance=value;
        invalid_unchanged(random,[&]{invoke(table(),{input({0,16}),bad});});
    }
    for (auto field : {0,1,2}) {
        auto bad=input({16,0});
        if (field==0) bad.goal.entry_flags=0;
        if (field==1) bad.goal.entry_flags=2;
        if (field==2) bad.goal.facing=8;
        invalid_unchanged(random,[&]{invoke(table(),{bad});});
    }
    auto bad=table(); bad.records[0].standing=2;
    invalid_unchanged(random,[&]{invoke(bad,{input({16,0})});});
    bad=table(0x55|4096);
    invalid_unchanged(random,[&]{invoke(bad,{input({16,0})});});
    bad=table(); bad.records[0].proximity=std::array<std::int32_t,3>{16,32,16};
    invalid_unchanged(random,[&]{invoke(bad,{input({16,0})});});
    invalid_unchanged(random,[&]{invoke(table(),{input({32768,0})});});
    invalid_unchanged(random,[&]{invoke(table(),{input({16,0})},{32768,0});});
    invalid_unchanged(random,[&]{invoke(table(),{input({-32768,-32768})});});
    invalid_unchanged(random,[&]{invoke(table(),std::vector<odh::ReferenceSlotScoreInput>(4097,input({16,0})));});
    const auto result=invoke(table(),std::vector<odh::ReferenceSlotScoreInput>(4096,input({16,0})));
    require(result.random_calls==4096 && result.ranked_goals.size()==4096,"inclusive 4096 candidate bound");
}
}
int main() {
    try { formulas(); branches(); door_range(); door_range_generation(); bed_north_circle_generation(); in_front_relative_generation(); house07_global_range(); child_shy_sitting_fallback(); invalid_inputs(); std::cout<<"slot scoring: "<<checks<<" checks passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
