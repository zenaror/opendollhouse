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
void source_full_circle() {
    auto circle=table(255);circle.records[0].max_size=100;circle.records[0].gradient_bits=0;
    const odh::EntityPosition target{64,64,1,0},caller{32,48,1,0};
    const auto geometry=odh::enumerate_source_slot_circle_candidates(circle,0,target,0.31f);
    const std::vector<odh::RoutePoint> expected{{64,80},{75,75},{80,64},{75,53},{64,48},{53,53},{48,64},{53,75}};
    require(geometry.size()==8,"source circle enumerates exactly eight bounded candidates");
    for(std::size_t n=0;n<geometry.size();++n){
        require(geometry[n].position==expected[n],"circle follows source S,SE,E,NE,N,NW,W,SW nearest-even order");
        const auto face=static_cast<float>(std::atan2(static_cast<double>(target.x-expected[n].x),
            static_cast<double>(expected[n].y-target.y)));
        require(geometry[n].radian_direction==face&&geometry[n].entry_flags!=0,
            "source circle computes FaceTowards from point geometry and continuous target sectors");
    }
    odh::RandomState random(7),draws(7);std::vector<odh::RoutePoint> visited;
    const auto goals=odh::rank_reference_range_slot(circle,0,caller,target,random,false,
        [&](auto point){return point==expected[0]||point==expected[2]||point==expected[5];},{},{},{},0.31f,
        [&](auto point){
            draws.next(1024);require(random.seed()==draws.seed(),"verifier observes its real RNG draw before placement verification");
            visited.push_back(point);
            return point==expected[0]?Verification::eligible_with_penalty:
                point==expected[2]?Verification::rejected_after_score:Verification::eligible;
        });
    require(visited==std::vector<odh::RoutePoint>{expected[0],expected[2],expected[5]}&&
        random.seed()==draws.seed()&&goals.size()==2&&goals.back().position==expected[0],
        "room filter precedes RNG; rejection consumes draw; avatar/door/destination penalty retains low-ranked option");
    auto directional=circle;directional.records[0].flags=0x8055;
    require(odh::is_source_slot_circle(directional.records[0]),
        "authored cardinal mask plus AllowDifferentAlts follows generic standing circle branch");
    odh::RandomState masked(9),masked_expected(9);std::vector<odh::RoutePoint> masked_visited;
    const auto masked_goals=odh::rank_reference_range_slot(directional,0,caller,target,masked,false,
        [](auto){return true;},{},{},{},0,[&](auto point){masked_visited.push_back(point);return Verification::eligible;});
    for(unsigned n=0;n<7;++n)masked_expected.next(1024);
    require(masked_visited==std::vector<odh::RoutePoint>{expected[0],expected[1],expected[2],expected[3],expected[4],expected[6],expected[7]}&&
        masked.seed()==masked_expected.seed()&&masked_goals.size()==7,
        "S-first circle geometry filters disallowed floor-tile direction BEFORE RNG and retains AllowDifferentAlts as unused source metadata");
    for(const auto& goal:masked_goals)require(goal.entry_flags&&(goal.entry_flags&~0x55U)==0&&goal.radian_direction,
        "masked source goals retain only accepted authored bits and exact continuous facing");
    auto shuffled=circle;shuffled.records[0].flags|=8192;odh::RandomState changing(55),reference(55);
    std::vector<std::pair<std::size_t,std::uint32_t>> scores;
    for(std::size_t n=0;n<8;++n){scores.emplace_back(n,reference.next(1024));reference.next(137);}
    std::stable_sort(scores.begin(),scores.end(),[](const auto& a,const auto& b){return a.second>b.second;});
    const auto effectful=odh::rank_reference_range_slot(shuffled,0,caller,target,changing,false,
        [](auto){return true;},{},{},{},0.31f,[&](auto){changing.next(137);return Verification::eligible;});
    require(changing.seed()==reference.seed()&&effectful.size()==8,
        "post-score verifier source effects consume shared RNG between each actual candidate draw");
    for(std::size_t n=0;n<8;++n)require(effectful[n].position==expected[scores[n].first],
        "next candidate score/rank uses actual RNG after preceding verifier effects rather than preview draws");
    auto tied=circle;tied.records[0].flags|=1U<<29;odh::RandomState equal(33);
    const auto ties=odh::rank_reference_range_slot(tied,0,target,target,equal,false,[](auto){return true;},{},{},{},0,
        [](auto){return Verification::eligible;});
    require(equal.seed()==33&&ties.size()==8&&ties[0].position==expected[1]&&ties[1].position==expected[3]&&
        ties[2].position==expected[5]&&ties[3].position==expected[7]&&ties[4].position==expected[0],
        "equal scores preserve enumeration ties; diagonal integer caller distance remains source15 rather than16");
    invalid_unchanged(random,[&]{(void)odh::rank_reference_range_slot(circle,0,caller,target,random,false,
        [](auto){return true;},{},{},{},0.31f);});
    invalid_unchanged(random,[&]{(void)odh::rank_reference_range_slot(circle,0,caller,target,random,false,
        [](auto){return true;},{},{},{},std::numeric_limits<float>::quiet_NaN(),[](auto){return Verification::eligible;});});
    for(const int radius:{1,23,32}){
        auto generic=circle;generic.records[0].proximity=std::array<std::int32_t,3>{radius,radius,0};
        require(odh::is_source_slot_circle(generic.records[0])&&
            odh::enumerate_source_slot_circle_candidates(generic,0,target,-0.4f).size()==8,
            "source profile accepts general bounded equal radii without resource or index special cases");
    }
    for(unsigned change=0;change<4;++change){
        auto bad=circle;
        if(change==0)bad.records[0].proximity=std::array<std::int32_t,3>{0,0,0};
        if(change==1)bad.records[0].sitting=1;
        if(change==2)bad.records[0].flags|=4096;
        if(change==3)bad.records[0].offset_bits[0]=0x3f800000;
        require(!odh::is_source_slot_circle(bad.records[0]),"zero radius, sitting, snap and offset policies remain outside full-circle profile");
    }
}
odh::SlotTable source_global_range_table(std::uint32_t flags=0x8055) {
    odh::SlotRecord slot{};
    slot.type=3;slot.offset_bits={0,0,0};slot.standing=1;slot.sitting=0;slot.ground=0;
    slot.flags=flags;slot.snap_target_slot=-1;
    slot.proximity=std::array<std::int32_t,3>{16,80,16};slot.max_size=0;slot.unknown_i10=0;
    slot.gradient_bits=0x3E400000;slot.height=0;slot.facing=-2;slot.resolution=16;
    return {0,10,{slot},0};
}
void source_standing_range() {
    auto slots=source_global_range_table();
    require(odh::is_source_standing_range_slot(slots.records[0]),
        "generic standing-range profile accepts audited Global SLOT100 type-3/index-3 semantics");
    const odh::EntityPosition caller{80,160,1,0},target{160,160,2,0};
    constexpr float target_angle=0.31f;
    const auto offsets=odh::enumerate_reference_slot_range_lattice(slots,0,4096);
    const auto ranged=odh::filter_reference_slot_range_proximity(slots,0,offsets);
    std::vector<odh::ReferenceSlotScoreInput> expected_inputs;
    std::vector<odh::RoutePoint> room_order,actual_room_order,verification_order;
    auto verification=[](odh::RoutePoint point) {
        if(point.x==160&&point.y==80) return Verification::rejected_after_score;
        return ((point.x+point.y)%5==0)?Verification::eligible_with_penalty:Verification::eligible;
    };
    for(const auto offset:ranged) {
        const odh::RoutePoint position{target.x+offset.x,target.y+offset.y};
        room_order.push_back(position); // room is queried before the direction mask.
        const auto flags=odh::reference_slot_entry_flags({target.x,target.y},target.direction,position,
            static_cast<std::uint8_t>(slots.records[0].flags),target_angle);
        if(!flags)continue;
        const auto dx=static_cast<float>(target.x)/16.0f-
                      (static_cast<float>(target.x)/16.0f+static_cast<float>(offset.x)/16.0f);
        const auto dy=-(static_cast<float>(target.y)/16.0f-
                      (static_cast<float>(target.y)/16.0f+static_cast<float>(offset.y)/16.0f));
        const auto radians=static_cast<float>(std::atan2(static_cast<double>(dx),static_cast<double>(dy)));
        const auto pi=3.14159265358979323846;
        const auto value=static_cast<double>(radians)/(pi/4.0);
        const auto lower=std::floor(value),fraction=value-lower;
        const auto raw=fraction<0.5?lower:fraction>0.5?lower+1.0:
            (std::fmod(std::abs(lower),2.0)==0.0?lower:lower+1.0);
        const auto facing=static_cast<std::uint8_t>((static_cast<int>(raw)%8+8)%8);
        const auto proximity=std::sqrt(static_cast<double>(offset.x)*offset.x+
                                        static_cast<double>(offset.y)*offset.y);
        odh::PreRankedSlotGoal goal{position,flags,facing};goal.radian_direction=radians;
        expected_inputs.push_back({goal,proximity,verification(position)});
    }
    odh::RandomState actual_random(0x6A09E667),expected_random(0x6A09E667),observed_draws(0x6A09E667);
    const auto expected=odh::score_reference_slot_candidates(slots,0,{target.x,target.y},
        {caller.x,caller.y},expected_inputs,expected_random);
    const auto actual=odh::rank_source_standing_range_slot(slots,0,caller,target,target_angle,actual_random,
        [&](odh::RoutePoint point){actual_room_order.push_back(point);return odh::SourceSlotRoomResult::same_room;},
        [&](odh::RoutePoint point){
            verification_order.push_back(point);observed_draws.next(1024);
            require(actual_random.seed()==observed_draws.seed(),
                "placement callback observes the candidate's already-published source score draw");
            return verification(point);
        });
    require(actual.size()==expected.ranked_goals.size()&&actual_random.seed()==expected_random.seed(),
        "generic range uses source score draws, rejections and stable ranked output");
    require(actual.size()==79&&actual.front().position==odh::RoutePoint{144,160}&&
            actual_random.seed()==UINT64_C(0x572F7B9A9B3B2215),
        "independent source arithmetic fixture pins first winner and all 80 candidate draws");
    require(verification_order.size()==expected_inputs.size(),
        "every direction-matched candidate reaches post-score verification, including rejected placement");
    for(std::size_t n=0;n<actual.size();++n) {
        const auto& a=actual[n];const auto& e=expected.ranked_goals[n];
        require(a.position==e.position&&a.entry_flags==e.entry_flags&&a.facing==e.facing&&
                a.radian_direction==e.radian_direction,
            "ranked generic range retains continuous FaceTowards angle and source tie order");
    }
    require(room_order.size()==ranged.size()&&actual_room_order==room_order,
        "room queries follow every in-range lattice candidate before direction filtering");
    auto same_level_caller=caller;same_level_caller.level=target.level;
    odh::RandomState same_level_random(0x6A09E667);
    const auto same_level=odh::rank_source_standing_range_slot(slots,0,same_level_caller,target,target_angle,
        same_level_random,[](auto){return odh::SourceSlotRoomResult::same_room;},
        [&](auto point){return verification(point);});
    require(same_level.size()==actual.size()&&same_level_random.seed()==actual_random.seed(),
        "source LotTilePos distance and ranking ignore level while room validation remains explicit");
    for(std::size_t n=0;n<actual.size();++n)
        require(same_level[n].position==actual[n].position&&same_level[n].radian_direction==actual[n].radian_direction,
            "cross-level caller uses the same 2D score ordering as the pinned LotTilePos distance");

    const auto before_room=actual_random.seed();bool unknown_room=false;
    try {
        (void)odh::rank_source_standing_range_slot(slots,0,caller,target,target_angle,actual_random,
            [](auto){return odh::SourceSlotRoomResult::unknown;},
            [](auto){return Verification::eligible;});
    } catch(const std::invalid_argument&) {unknown_room=true;}
    require(unknown_room&&actual_random.seed()==before_room,
        "unknown room evidence fails closed before scoring the first in-range candidate");
    odh::RandomState different_room_random(23),different_room_seed(23);std::size_t different_room_checks=0,unexpected_placement=0;
    const auto different_room=odh::rank_source_standing_range_slot(slots,0,caller,target,target_angle,
        different_room_random,[&](auto){++different_room_checks;return odh::SourceSlotRoomResult::different_room;},
        [&](auto){++unexpected_placement;return Verification::eligible;});
    require(different_room.empty()&&different_room_checks==ranged.size()&&unexpected_placement==0&&
            different_room_random.seed()==different_room_seed.seed(),
        "different-room candidates are removed before scoring and never reach placement verification");
    bool invalid_room=false;odh::RandomState invalid_room_random(29);
    try {
        (void)odh::rank_source_standing_range_slot(slots,0,caller,target,target_angle,invalid_room_random,
            [](auto){return static_cast<odh::SourceSlotRoomResult>(99);},
            [](auto){return Verification::eligible;});
    } catch(const std::invalid_argument&) {invalid_room=true;}
    require(invalid_room&&invalid_room_random.seed()==29,
        "invalid room enum values fail closed rather than acting as same-room evidence");
    bool unknown_placement=false;odh::RandomState unknown_random(19),one_draw(19);
    try {
        (void)odh::rank_source_standing_range_slot(slots,0,caller,target,target_angle,unknown_random,
            [](auto){return odh::SourceSlotRoomResult::same_room;},
            [](auto){return Verification::unknown;});
    } catch(const std::invalid_argument&) {unknown_placement=true;}
    one_draw.next(1024);
    require(unknown_placement&&unknown_random.seed()==one_draw.seed(),
        "unknown placement evidence is reported after and retains its source score draw");
    bool invalid_placement=false;odh::RandomState invalid_placement_random(37),invalid_placement_draw(37);
    try {
        (void)odh::rank_source_standing_range_slot(slots,0,caller,target,target_angle,invalid_placement_random,
            [](auto){return odh::SourceSlotRoomResult::same_room;},
            [](auto){return static_cast<Verification>(99);});
    } catch(const std::invalid_argument&) {invalid_placement=true;}
    invalid_placement_draw.next(1024);
    require(invalid_placement&&invalid_placement_random.seed()==invalid_placement_draw.seed(),
        "invalid placement enum values fail closed after the source score draw");
    odh::RandomState no_room_random(31);const auto no_room_seed=no_room_random.seed();bool missing_room=false;
    try {
        (void)odh::rank_source_standing_range_slot(slots,0,caller,target,target_angle,no_room_random,{},
            [](auto){return Verification::eligible;});
    } catch(const std::invalid_argument&) {missing_room=true;}
    require(missing_room&&no_room_random.seed()==no_room_seed,
        "missing required room evidence rejects the profile before RNG");

    auto ignore_rooms=source_global_range_table(0x8055|2048U);
    odh::RandomState ignored_random(7);bool room_called=false;
    const auto ignored=odh::rank_source_standing_range_slot(ignore_rooms,0,caller,target,target_angle,ignored_random,
        [&](auto){room_called=true;return odh::SourceSlotRoomResult::unknown;},
        [](auto){return Verification::eligible;});
    require(!ignored.empty()&&!room_called,
        "IgnoreRooms bypasses the room capability rather than requiring or fabricating a room result");

    auto absolute=source_global_range_table(0x8055|512U);
    odh::RandomState absolute_random(7);
    const auto absolute_goals=odh::rank_source_standing_range_slot(absolute,0,caller,target,target_angle,absolute_random,
        [](auto){return odh::SourceSlotRoomResult::same_room;},[](auto){return Verification::eligible;});
    require(!absolute_goals.empty()&&absolute_goals.front().radian_direction,
        "Absolute changes sector origin to north but FaceTowards remains continuous to the target object");
    for(const auto& goal:absolute_goals)
        require(goal.entry_flags==odh::reference_slot_entry_flags({target.x,target.y},target.direction,
            goal.position,0x55U,0.0f),"Absolute slot directions are evaluated in world orientation");

    auto excessive=source_global_range_table();excessive.records[0].proximity=
        std::array<std::int32_t,3>{1,4096,1};excessive.records[0].resolution=1;
    odh::RandomState excessive_random(41);std::size_t excessive_callbacks=0;bool lattice_bounded=false;
    try {
        (void)odh::rank_source_standing_range_slot(excessive,0,caller,target,target_angle,excessive_random,
            [&](auto){++excessive_callbacks;return odh::SourceSlotRoomResult::same_room;},
            [&](auto){++excessive_callbacks;return Verification::eligible;});
    } catch(const std::length_error&) {lattice_bounded=true;}
    require(lattice_bounded&&excessive_callbacks==0&&excessive_random.seed()==41,
        "oversized source lattice rejects before callbacks or RNG consumption");

    auto bad=slots;bad.records[0].flags|=4096U;
    require(!odh::is_source_standing_range_slot(bad.records[0]),
        "SnapToDirection remains outside the non-snap range path");
    bad=slots;bad.records[0].flags|=65536U;
    require(!odh::is_source_standing_range_slot(bad.records[0]),
        "UseAverageObjectLocation requires a distinct multitile-aware center capability");
    bad=slots;bad.records[0].proximity=std::array<std::int32_t,3>{16,16,16};
    require(!odh::is_source_standing_range_slot(bad.records[0]),
        "equal proximity belongs to the separate source circle branch");
    invalid_unchanged(actual_random,[&]{(void)odh::rank_source_standing_range_slot(slots,0,caller,target,
        std::numeric_limits<float>::quiet_NaN(),actual_random,{},[](auto){return Verification::eligible;});});

    auto out_of_band=source_global_range_table();out_of_band.records[0].proximity=
        std::array<std::int32_t,3>{16,80,100};
    odh::RandomState signed_formula(5),signed_draw(5);
    const auto draw=static_cast<float>(signed_draw.next(1024))/1024.0f;
    const auto scored=odh::score_reference_slot_candidates(out_of_band,0,{160,160},{80,160},
        {{{{176,160},1,2},16,Verification::eligible}},signed_formula);
    const auto source_max_score=std::max(100-16,80-100)+(80+80)/3+2;
    const auto source_base=draw+source_max_score-std::abs(100-16);
    require(scored.records.front().base_score==source_base&&
            *scored.records.front().final_score==source_base-96/3.0,
        "generic source scoring preserves signed maxScore terms when desired proximity lies outside the range");

    auto weighted=source_global_range_table();weighted.records[0].standing=2;
    odh::RandomState negative_score_random(1),negative_draw(1);
    const auto negative_random=static_cast<float>(negative_draw.next(1024))/1024.0f;
    const auto negative=odh::score_reference_slot_candidates(weighted,0,{0,0},{-20000,0},
        {{{{26340,0},1,0},16,Verification::eligible_with_penalty}},negative_score_random);
    const auto negative_base=negative_random+64.0+(20000+80)/3+2-std::abs(16-16);
    const auto negative_after_distance=(negative_base-46340/3.0)*2.0/100000.0;
    require(negative.records.front().final_score&&*negative.records.front().final_score==negative_after_distance&&
            *negative.records.front().final_score<0,
        "Standing weight multiplies a negative score before the source occupied-goal penalty");
}
void source_face_anywhere_range() {
    auto slots=source_global_range_table(0xA0FF);auto& row=slots.records.front();
    row.proximity=std::array<std::int32_t,3>{16,64,16};row.resolution=8;
    row.gradient_bits=0;row.facing=-3;
    const odh::EntityPosition caller{424,424,1,0},target{424,424,1,0};
    require(odh::is_source_standing_range_slot(row),"FaceAnywhere standing range uses the shared bounded source classifier");
    odh::RandomState random(0x6A09E667),draws(0x6A09E667),towards_random(0x6A09E667);
    std::vector<odh::RoutePoint> verified;
    const auto verify=[](odh::RoutePoint point){return point.x==432&&point.y==456?
        Verification::rejected_after_score:Verification::eligible;};
    const auto anywhere=odh::rank_source_standing_range_slot(slots,0,caller,target,0,random,
        [](auto){return odh::SourceSlotRoomResult::same_room;},[&](auto point){
            verified.push_back(point);draws.next(1024);
            require(random.seed()==draws.seed(),"FaceAnywhere live verification sees its already-consumed source candidate RNG draw");
            return verify(point);
        });
    auto towards_slots=slots;towards_slots.records.front().facing=-2;
    const auto towards=odh::rank_source_standing_range_slot(towards_slots,0,caller,target,0,towards_random,
        [](auto){return odh::SourceSlotRoomResult::same_room;},verify);
    require(!anywhere.empty()&&anywhere.size()==towards.size()&&random.seed()==towards_random.seed()&&
        verified.size()==anywhere.size()+1,"FaceAnywhere preserves candidate scoring order and post-score rejection draws");
    for(std::size_t n=0;n<anywhere.size();++n){const auto& goal=anywhere[n];
        require(goal.position==towards[n].position&&goal.entry_flags==towards[n].entry_flags&&
            goal.facing==0&&goal.radian_direction&&*goal.radian_direction==0&&!std::signbit(*goal.radian_direction),
            "FaceAnywhere emits source positive-zero radians and facing0 without changing ranked positions or flags");}
    const auto geometry=odh::enumerate_source_slot_range_candidates(slots,0,target,0);
    require(!geometry.empty(),"FaceAnywhere range exposes bounded source geometry");
    for(const auto& goal:geometry)require(goal.facing==0&&goal.radian_direction==0&&!std::signbit(goal.radian_direction),
        "FaceAnywhere pure geometry agrees with the scorer's neutral arrival direction");
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
void canonical_transient_relative_ranges() {
    constexpr double pi=3.141592653589793238462643383279502884;
    const odh::EntityPosition caller{48,80,1,0},target{80,80,1,3};
    for(const auto location:{0,1,2,3,4,5,6,7,255})for(const auto direction:{0,1,2,3,4,5,6,7,254,255}){
        odh::Instruction instruction{27,254,255,{5,0,static_cast<std::uint8_t>(location),
            static_cast<std::uint8_t>(direction),0,0,3,0}};
        const auto canonical=odh::make_source_goto_relative_slot(instruction);
        require(canonical&&canonical->records.size()==1&&canonical->version==10,
            "source relative instruction normalizes into one transient SLOT at index zero");
        const auto& row=canonical->records.front();
        require(row.proximity==std::optional<std::array<std::int32_t,3>>{{16,location==255?32:24,location==255?16:0}}&&
            row.flags==(location==255?255U:1U<<location)&&row.max_size==100&&row.gradient_bits==0,
            "transient SLOT preserves directional Optimal0 and explicit AnywhereNear Optimal16 defaults");
        auto counters=instruction;counters.operands[0]=231;counters.operands[1]=127;
        counters.operands[4]=255;counters.operands[5]=254;
        const auto other=odh::make_source_goto_relative_slot(counters);
        require(other&&other->records.front().proximity==row.proximity&&other->records.front().flags==row.flags&&
            other->records.front().facing==row.facing,
            "unused old counters never become a transient SLOT index or alter its geometry");
        odh::RandomState random{71},expected{71};unsigned verified=0;
        const auto goals=odh::rank_source_standing_range_slot(*canonical,0,caller,target,2.3561945f,random,
            [](auto){return odh::SourceSlotRoomResult::same_room;},[&](auto){++verified;
                return odh::ReferenceSlotVerification::eligible;});
        const auto geometry=odh::enumerate_source_slot_range_candidates(*canonical,0,target,2.3561945f);
        require(!goals.empty()&&verified==geometry.size()&&goals.size()==geometry.size(),
            "all source sectors and authored facing modes share the standing range verifier");
        for(std::size_t n=0;n<verified;++n)expected.next(1024);
        require(random.seed()==expected.seed(),"shared relative range scoring draws once before each actual verification");
        for(const auto& goal:goals){
            require(goal.radian_direction.has_value(),"relative range retains exact continuous arrival direction");
            if(direction==255)require(*goal.radian_direction==0&&goal.facing==0,
                "AnyDirection keeps the source neutral arrival without imposing a facing");
            if(direction<8){
                const auto rotated=(direction+3)%8;
                const float source=static_cast<float>(rotated>4?static_cast<double>(rotated)*pi/4.0:
                    static_cast<double>(rotated-8)*pi/4.0);
                require(*goal.radian_direction==source&&goal.facing==rotated,
                    "authored direction uses the target's discrete facing and source signed-angle convention");
            }
        }
    }
    odh::Instruction exact{27,254,255,{0,0,254,254,0,0,0,0}};
    const auto point=odh::make_source_goto_relative_slot(exact);
    require(point&&point->records.front().flags==0&&
        point->records.front().proximity==std::optional<std::array<std::int32_t,3>>{{0,0,0}},
        "OnTopOf normalizes to the source zero-distance transient SLOT");
    auto invalid=exact;invalid.operands[2]=8;
    require(!odh::make_source_goto_relative_slot(invalid),"unknown relative locations never obtain canonical SLOT authority");
}
void in_front_relative_generation() {
    odh::SlotRecord relative{};
    relative.type=3;relative.flags=0x01;relative.offset_bits={0,0,0};
    relative.standing=1;relative.sitting=0;relative.ground=0;relative.snap_target_slot=-1;
    relative.proximity=std::array<std::int32_t,3>{16,24,0};relative.max_size=100;
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
void relative_directions_and_near() {
    odh::SlotRecord slot{};
    slot.type=3;slot.offset_bits={0,0,0};slot.standing=1;slot.sitting=0;
    slot.ground=0;slot.snap_target_slot=-1;slot.max_size=100;slot.unknown_i10=0;
    slot.gradient_bits=0;slot.height=0;slot.resolution=16;
    const odh::EntityPosition caller{48,80,1,0},target{80,80,1,3};
    for(unsigned location=0;location<9;++location) {
        slot.flags=location==8?255U:1U<<location;
        slot.proximity=std::array<std::int32_t,3>{16,location==8?32:24,location==8?16:0};
        for(const int facing:{-3,-2,0,1,2,3,4,5,6,7}) {
            slot.facing=facing;
            const odh::SlotTable table{0,10,{slot},0};
            odh::RandomState random(7),rejected(7),expected(7);
            const auto goals=odh::rank_reference_range_slot(table,0,caller,target,random);
            require(!goals.empty(),"all relative sectors and AnywhereNear have standing candidates");
            for(const auto& goal:goals) {
                const auto flags=odh::reference_slot_entry_flags({80,80},3,goal.position,
                    static_cast<std::uint8_t>(slot.flags));
                require(flags&&goal.entry_flags==flags,"relative sector stays anchored to target orientation");
                if(facing>=0)require(goal.facing==((3+facing)&7),
                    "relative facing offset is independent of selected approach sector");
            }
            const auto blocked=odh::rank_reference_range_slot(table,0,caller,target,rejected,false,
                [](auto){return true;},[](auto){return false;});
            for(std::size_t n=0;n<goals.size();++n)expected.next(1024);
            require(blocked.empty()&&random.seed()==expected.seed()&&rejected.seed()==expected.seed(),
                "each sector candidate consumes one score draw even when blocked");
            if(location==8)require(goals.size()==12,"AnywhereNear includes axes at two tiles and diagonals");
        }
    }
}
void continuous_relative_sector() {
    odh::SlotRecord slot{};
    slot.type=3;slot.flags=1;slot.offset_bits={0,0,0};slot.standing=1;slot.sitting=0;
    slot.ground=0;slot.snap_target_slot=-1;slot.proximity=std::array<std::int32_t,3>{16,24,0};
    slot.max_size=100;slot.unknown_i10=0;slot.gradient_bits=0;slot.height=0;slot.facing=-2;slot.resolution=16;
    const odh::SlotTable table{0,10,{slot},0};
    odh::RandomState random(7),expected(7);
    const auto goals=odh::rank_reference_range_slot(table,0,{48,80,1,0},{80,80,1,0},random,
        false,{},{},{},{},0.31f);
    require(goals.size()==2,"between-notch target angle admits north and northeast in source front sector");
    for(const auto& goal:goals)require(goal.position==odh::RoutePoint{80,64}||goal.position==odh::RoutePoint{96,64},
        "continuous source angle changes search sectors without rotating the lattice");
    expected.next(1024);expected.next(1024);
    require(random.seed()==expected.seed(),"continuous sector consumes both source score draws");
    const auto before=random.seed();bool rejected=false;
    try{(void)odh::rank_reference_range_slot(table,0,{48,80,1,0},{80,80,1,0},random,
        false,{},{},{},{},std::numeric_limits<float>::quiet_NaN());}
    catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&before==random.seed(),"nonfinite source angle rejects before scoring RNG");
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
    auto weighted=table();weighted.records[0].standing=2;
    odh::RandomState unit_random(99),weighted_random(99);
    const auto unit=odh::score_reference_slot_candidates(table(),0,{0,0},{0,0},{input({16,0})},unit_random);
    const auto doubled=odh::score_reference_slot_candidates(weighted,0,{0,0},{0,0},{input({16,0})},weighted_random);
    require(unit.records[0].final_score&&doubled.records[0].final_score&&
        *doubled.records[0].final_score==2*(*unit.records[0].final_score)&&unit_random.seed()==weighted_random.seed(),
        "generic source circle Standing2 is valid and doubles its actual score without extra RNG");
    for(const auto standing:{-1,0,32768}){
        auto invalid=table();invalid.records[0].standing=standing;
        invalid_unchanged(random,[&]{invoke(invalid,{input({16,0})});});
    }
    auto bad=table(0x55|4096);
    invalid_unchanged(random,[&]{invoke(bad,{input({16,0})});});
    auto generic_range=table();generic_range.records[0].proximity=std::array<std::int32_t,3>{16,32,16};
    odh::RandomState generic_range_random(13);
    const auto generic_range_result=odh::score_reference_slot_candidates(generic_range,0,{0,0},{0,0},
        {input({16,0}),{{{24,0},1,2},24,Verification::eligible}},generic_range_random);
    require(generic_range_result.random_calls==2&&generic_range_result.ranked_goals.size()==2,
        "source-shaped 16-to-32 standing range is accepted by the generic arithmetic scorer");
    bad=table(); bad.records[0].proximity=std::array<std::int32_t,3>{32,16,16};
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
    try { canonical_transient_relative_ranges(); source_full_circle(); source_standing_range(); source_face_anywhere_range(); formulas(); branches(); door_range(); door_range_generation(); bed_north_circle_generation(); in_front_relative_generation(); relative_directions_and_near(); continuous_relative_sector(); house07_global_range(); child_shy_sitting_fallback(); invalid_inputs(); std::cout<<"slot scoring: "<<checks<<" checks passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
