#include "odh/slot_scoring.hpp"
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
    try { formulas(); branches(); door_range(); door_range_generation(); house07_global_range(); invalid_inputs(); std::cout<<"slot scoring: "<<checks<<" checks passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
