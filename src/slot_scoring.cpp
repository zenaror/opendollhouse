#include "odh/slot_scoring.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
constexpr std::uint32_t random_scoring = 8192;
constexpr std::uint32_t equal_proximity = 1U << 29;
bool child_shy_slot_record(const SlotRecord* slot) {
    return slot && slot->type==3 && slot->flags==0x8055 &&
        slot->offset_bits==std::array<std::uint32_t,3>{0,0,0} &&
        slot->proximity==std::optional<std::array<std::int32_t,3>>{{32,80,32}} &&
        slot->standing==1 && slot->sitting==10 && slot->ground==0 &&
        slot->snap_target_slot==-1 && slot->max_size==0 && slot->unknown_i10==0 &&
        slot->gradient_bits==0x3E400000 && slot->height==0 && slot->facing==-2 &&
        slot->resolution==16;
}
bool in_front_relative_slot_record(const SlotRecord* slot) {
    // VMGotoRelativePosition builds this transient SLOT for InFrontOf/Facing.
    // SLOTItem defaults MaxSize=100, Gradient=0, Standing=1, Sitting=0,
    // FaceTowardsObject and Resolution=16.
    return slot && slot->type==3 && slot->flags==0x01 &&
        slot->offset_bits==std::array<std::uint32_t,3>{0,0,0} &&
        slot->proximity==std::optional<std::array<std::int32_t,3>>{{16,24,16}} &&
        slot->standing==1 && slot->sitting==0 && slot->ground==0 &&
        slot->snap_target_slot==-1 && slot->max_size==100 && slot->unknown_i10==0 &&
        slot->gradient_bits==0 && slot->height==0 && slot->facing==-2 &&
        slot->resolution==16;
}
void validate_position(RoutePoint p) {
    if (p.x < -32768 || p.x > 32767 || p.y < -32768 || p.y > 32767)
        throw std::invalid_argument("reference SLOT score position exceeds LotTilePos short coordinates");
}
std::int32_t distance(RoutePoint a, RoutePoint b) {
    const auto dx = static_cast<std::int64_t>(a.x) - b.x;
    const auto dy = static_cast<std::int64_t>(a.y) - b.y;
    const auto square = dx * dx + dy * dy;
    // The reference multiplies signed ints. Do not emulate overflow inputs.
    if (square > std::numeric_limits<std::int32_t>::max())
        throw std::invalid_argument("reference SLOT distance exceeds safe reference integer arithmetic");
    return static_cast<std::int32_t>(std::sqrt(static_cast<double>(square)));
}
}
bool is_child_shy_sitting_fallback_slot(const SlotTable& table,std::size_t type3_index) {
    return table.version==10 && type3_index==10 &&
        child_shy_slot_record(slot_of_type(table,3,type3_index));
}
std::optional<std::string> child_shy_sitting_candidate_inventory_preflight(
    const World& world,const SlotTable& table,std::size_t type3_index,const EntityPosition& target) {
    if(!is_child_shy_sitting_fallback_slot(table,type3_index))
        return "child-shy standing fallback received an unverified SLOT profile";
    auto object_id=world.next_entity(std::numeric_limits<std::int16_t>::min(),EntityKind::object);
    while(object_id) {
        const auto* object=world.find(*object_id);
        if(!object)return "World object inventory changed during child-shy SLOT preflight";
        if(!object->position) {
            object_id=world.next_entity(*object_id,EntityKind::object);
            continue;
        } else if(object->position->level==target.level) {
            // We do not yet have the exact footprint-aware candidate test used
            // by Simitone. Inspect every object on this floor; origin distance
            // cannot prove that a large/multitile seat misses the candidate ring.
            if(!object->entry_points||object->entry_points->size()<=26)
                return "object "+std::to_string(*object_id)+
                    " has no complete EntryPoint26 table; sitting target inventory is incomplete";
            // A non-zero action is resolved against each candidate's saved
            // footprint; object origin alone cannot select or rule out a seat.
        }
        object_id=world.next_entity(*object_id,EntityKind::object);
    }
    return {};
}
ReferenceSlotScores score_reference_slot_candidates(
    const SlotTable& table, std::size_t type3_index, RoutePoint object_position,
    RoutePoint caller_position, const std::vector<ReferenceSlotScoreInput>& inputs,
    RandomState& random,bool no_sitting_target_proven,bool sitting_candidates_resolved) {
    const auto* slot = slot_of_type(table, 3, type3_index);
    const bool shy_child_fallback=(no_sitting_target_proven||sitting_candidates_resolved)&&
        is_child_shy_sitting_fallback_slot(table,type3_index);
    const bool relative_front=in_front_relative_slot_record(slot);
    const bool common=table.version == 10 && slot &&
        slot->offset_bits == std::array<std::uint32_t,3>{0,0,0} && slot->standing == 1 &&
        (slot->sitting == 0 || shy_child_fallback) && slot->ground == 0 && slot->snap_target_slot == -1 &&
        (slot->max_size == 0 || relative_front) && slot->unknown_i10 == 0 &&
        (slot->gradient_bits == 0x3E400000 || relative_front) &&
        slot->height == 0 && slot->proximity &&
        (*slot->proximity)[0] > 0 && (*slot->proximity)[1] >= (*slot->proximity)[0] &&
        (*slot->proximity)[2] >= (*slot->proximity)[0] && (*slot->proximity)[0] <= 4096 &&
        (*slot->proximity)[1] <= 4096;
    const bool circle=common &&
        slot->facing == -2 && slot->resolution == 16 &&
        (slot->flags & ~(random_scoring | equal_proximity)) == 0x55 &&
        (*slot->proximity)[0] == (*slot->proximity)[1] &&
        (*slot->proximity)[0] == (*slot->proximity)[2];
    const auto door_flags=slot?(slot->flags & ~(random_scoring | equal_proximity)):0U;
    const bool door_range=common && slot->facing == -2 && slot->resolution == 16 &&
        ((slot->flags & 0xFFU) == 0x54U || (slot->flags & 0xFFU) == 0x45U) &&
        (door_flags == 0x54U || door_flags == 0x45U) &&
        *slot->proximity == std::array<std::int32_t,3>{16,32,16};
    const bool house07_global_range=table.version==10 && slot && slot->flags==0xA0FF &&
        slot->offset_bits==std::array<std::uint32_t,3>{0,0,0} && slot->standing==1 &&
        slot->sitting==0 && slot->ground==0 && slot->snap_target_slot==-1 &&
        slot->max_size==0 && slot->unknown_i10==0 && slot->gradient_bits==0 &&
        slot->height==0 && slot->facing==-3 && slot->resolution==8 &&
        slot->proximity==std::optional<std::array<std::int32_t,3>>{{16,64,16}};
    if (!circle && !door_range && !house07_global_range && !shy_child_fallback && !relative_front)
        throw std::invalid_argument("reference SLOT scoring requires a verified House07 circle or ranged profile");
    if (inputs.size() > 4096) throw std::length_error("reference SLOT scoring exceeds 4096 candidates");
    validate_position(object_position);
    validate_position(caller_position);
    const auto min_proximity = (*slot->proximity)[0];
    const auto max_proximity = (*slot->proximity)[1] == 0 ? min_proximity : (*slot->proximity)[1];
    const auto desired_proximity = (*slot->proximity)[2] == 0 ? min_proximity : (*slot->proximity)[2];
    // Both divisions are intentionally different: maxScore uses integer /3,
    // while VerifyAndAddLocation subtracts integer distance divided by 3.0.
    const double max_score = std::max(std::abs(desired_proximity-min_proximity),
        std::abs(max_proximity-desired_proximity)) +
        (distance(object_position, caller_position) + max_proximity) / 3 + 2;
    std::vector<std::int32_t> caller_distances;
    caller_distances.reserve(inputs.size());
    for (const auto& input : inputs) {
        validate_position(input.goal.position);
    const bool in_range=std::isfinite(input.slot_distance) && (circle
            ? input.slot_distance == min_proximity
            : input.slot_distance >= min_proximity-0.5 && input.slot_distance <= max_proximity+0.5);
        if (!in_range || input.goal.entry_flags == 0 ||
            (input.goal.entry_flags & ~static_cast<std::uint8_t>(slot->flags)) != 0 ||
            input.goal.facing > 7 ||
            (input.goal.sitting_object &&
             (!sitting_candidates_resolved || !shy_child_fallback || *input.goal.sitting_object <= 0)) ||
            (input.goal.sitting_approach && !input.goal.sitting_object) ||
            (input.verification != ReferenceSlotVerification::eligible &&
             input.verification != ReferenceSlotVerification::eligible_with_penalty &&
             input.verification != ReferenceSlotVerification::rejected_after_score))
            throw std::invalid_argument("reference SLOT scoring requires known verification and valid pre-score metadata");
        caller_distances.push_back(distance(input.goal.position, caller_position));
    }
    ReferenceSlotScores result{{},{},{},random.seed(),random.seed(),0};
    result.records.reserve(inputs.size());
    result.ranked_goals.reserve(inputs.size());
    result.ranked_input_indices.reserve(inputs.size());
    auto next_random = random;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        const auto& input = inputs[i];
        const auto before = next_random.seed();
        double base = max_score;
        if ((slot->flags & equal_proximity) == 0) {
            base = static_cast<float>(next_random.next(1024)) / 1024.0f;
            ++result.random_calls;
            if ((slot->flags & random_scoring) == 0)
                base += max_score - std::abs(desired_proximity - input.slot_distance);
        }
        std::optional<double> score;
        if (input.verification != ReferenceSlotVerification::rejected_after_score) {
            score = base;
            if ((slot->flags & random_scoring) == 0) *score -= caller_distances[i] / 3.0;
            *score *= input.goal.sitting_object ? slot->sitting : slot->standing;
            if (input.verification == ReferenceSlotVerification::eligible_with_penalty) *score /= 100000;
            result.ranked_input_indices.push_back(i);
        }
        result.records.push_back({base,score,input.verification,before,next_random.seed()});
    }
    std::stable_sort(result.ranked_input_indices.begin(), result.ranked_input_indices.end(),
        [&](std::size_t a, std::size_t b) { return *result.records[a].final_score > *result.records[b].final_score; });
    for (const auto i : result.ranked_input_indices) result.ranked_goals.push_back(inputs[i].goal);
    result.seed_after = next_random.seed();
    random = next_random;
    return result;
}
std::vector<PreRankedSlotGoal> rank_reference_range_slot(
    const SlotTable& table,std::size_t type3_index,const EntityPosition& caller,
    const EntityPosition& target,RandomState& random,bool no_sitting_target_proven,
    const std::function<bool(RoutePoint)>& candidate_in_room,
    const std::function<bool(RoutePoint)>& candidate_position_valid,
    const std::function<std::optional<std::int16_t>(RoutePoint,std::uint8_t)>& candidate_sitting_object,
    const std::function<std::optional<RoutePoint>(RoutePoint)>& candidate_sitting_approach) {
    const auto* slot=slot_of_type(table,3,type3_index);
    const bool relative_front=in_front_relative_slot_record(slot);
    const bool sitting_candidates_resolved=static_cast<bool>(candidate_sitting_object);
    const bool shy_child_fallback=(no_sitting_target_proven||sitting_candidates_resolved)&&
        is_child_shy_sitting_fallback_slot(table,type3_index);
    const bool door_range=table.version==10&&slot&&
       (slot->flags==0x54||slot->flags==0x45)&&slot->facing==-2&&
       slot->offset_bits==std::array<std::uint32_t,3>{0,0,0}&&
       slot->proximity==std::optional<std::array<std::int32_t,3>>{{16,32,16}}&&
       slot->resolution==16&&slot->standing==1&&slot->sitting==0&&slot->ground==0&&
       slot->snap_target_slot==-1&&slot->max_size==0&&slot->unknown_i10==0&&
       slot->gradient_bits==0x3E400000&&slot->height==0;
    const bool house07_global_range=table.version==10&&slot&&slot->flags==0xA0FF&&
       slot->facing==-3&&slot->offset_bits==std::array<std::uint32_t,3>{0,0,0}&&
       slot->proximity==std::optional<std::array<std::int32_t,3>>{{16,64,16}}&&
       slot->resolution==8&&slot->standing==1&&slot->sitting==0&&slot->ground==0&&
       slot->snap_target_slot==-1&&slot->max_size==0&&slot->unknown_i10==0&&
       slot->gradient_bits==0&&slot->height==0;
    if(!door_range&&!house07_global_range&&!shy_child_fallback&&!relative_front)
        throw std::invalid_argument("ranged SLOT generation requires a source-verified profile");
    const RoutePoint center{target.x,target.y};
    const auto offsets=enumerate_reference_slot_range_lattice(table,type3_index,4096);
    const auto ranged=filter_reference_slot_range_proximity(table,type3_index,offsets);
    constexpr std::array<RoutePoint,8> headings={{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
    std::vector<ReferenceSlotScoreInput> inputs;
    inputs.reserve(ranged.size());
    for(const auto offset:ranged) {
        const auto x=static_cast<std::int64_t>(center.x)+offset.x;
        const auto y=static_cast<std::int64_t>(center.y)+offset.y;
        if(x<-32768||x>32767||y<-32768||y>32767)
            throw std::invalid_argument("Door range candidate exceeds LotTilePos short coordinates");
        const RoutePoint position{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)};
        const auto entry_flags=reference_slot_entry_flags(center,target.direction,position,
            static_cast<std::uint8_t>(slot->flags));
        if(entry_flags==0)continue;
        // The reference checks room before scoring, but applies the later
        // PositionValid/obstacle checks only after consuming its random score.
        if(candidate_in_room&&!candidate_in_room(position))continue;
        const auto dx=(target.x>position.x)-(target.x<position.x);
        const auto dy=(target.y>position.y)-(target.y<position.y);
        std::uint8_t facing=0;
        if(slot->facing!=-3) {
            for(std::uint8_t n=0;n<headings.size();++n)
                if(headings[n]==RoutePoint{dx,dy}){facing=n;break;}
        }
        const auto ox=static_cast<double>(offset.x),oy=static_cast<double>(offset.y);
        const auto proximity=std::sqrt(ox*ox+oy*oy);
        std::optional<std::int16_t> sitting_object;
        std::optional<RoutePoint> sitting_approach;
        bool position_valid=!candidate_position_valid||candidate_position_valid(position);
        if(sitting_candidates_resolved&&shy_child_fallback) {
            sitting_object=candidate_sitting_object(position,facing);
            if(sitting_object&&*sitting_object<=0)sitting_object.reset();
            if(sitting_object&&!position_valid&&candidate_sitting_approach) {
                sitting_approach=candidate_sitting_approach(position);
                if(sitting_approach&&candidate_position_valid)
                    position_valid=candidate_position_valid(*sitting_approach);
            }
            if(!position_valid)sitting_object.reset();
        }
        const auto verification=position_valid
            ?ReferenceSlotVerification::eligible
            :ReferenceSlotVerification::rejected_after_score;
        inputs.push_back({{position,entry_flags,facing,sitting_object,sitting_approach},proximity,verification});
    }
    return score_reference_slot_candidates(table,type3_index,center,{caller.x,caller.y},inputs,random,
        no_sitting_target_proven,sitting_candidates_resolved).ranked_goals;
}
} // namespace odh
