#include "odh/routing.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
constexpr std::array<RoutePoint,8> headings={{{0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}}};
std::uint8_t heading(RoutePoint from,RoutePoint to) {
    const RoutePoint d{(to.x>from.x)-(to.x<from.x),(to.y>from.y)-(to.y<from.y)};
    for(std::uint8_t n=0;n<8;++n) if(headings[n]==d) return n;
    return 0;
}
PrimitiveOutcome unsupported(const std::string& why) {return {PrimitiveDisposition::unsupported,why};}
bool supported_circle_slot_profile(const SlotRecord& slot) {
    const bool house07=slot.flags==0x55&&slot.facing==-2;
    const bool door_north=slot.flags==0x0801&&slot.facing==-3;
    const bool door_south=slot.flags==0x0810&&slot.facing==-3;
    return (house07||door_north||door_south) &&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0} &&
        slot.standing==1 && slot.sitting==0 && slot.ground==0 &&
        slot.snap_target_slot==-1 && slot.max_size==0 && slot.unknown_i10==0 &&
        slot.gradient_bits==0x3E400000 && slot.height==0 && slot.proximity &&
        slot.resolution==16 && (*slot.proximity)[0]>0 &&
        (*slot.proximity)[0]==(*slot.proximity)[1] &&
        (*slot.proximity)[0]==(*slot.proximity)[2] && (*slot.proximity)[0]<=4096;
}
bool supported_ranged_slot_profile(const SlotRecord& slot) {
    const bool door_range=(slot.flags==0x54||slot.flags==0x45)&&slot.facing==-2&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&slot.proximity&&
        *slot.proximity==std::array<std::int32_t,3>{16,32,16}&&
        slot.standing==1&&slot.sitting==0&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==0&&slot.unknown_i10==0&&slot.gradient_bits==0x3E400000&&
        slot.height==0&&slot.resolution==16;
    const bool house07_global_range=slot.flags==0xA0FF&&slot.facing==-3&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&slot.proximity&&
        *slot.proximity==std::array<std::int32_t,3>{16,64,16}&&
        slot.standing==1&&slot.sitting==0&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==0&&slot.unknown_i10==0&&slot.gradient_bits==0&&
        slot.height==0&&slot.resolution==8;
    const bool in_front_relative=slot.type==3&&slot.flags==0x01&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&slot.proximity&&
        *slot.proximity==std::array<std::int32_t,3>{16,24,16}&&
        slot.standing==1&&slot.sitting==0&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==100&&slot.unknown_i10==0&&slot.gradient_bits==0&&
        slot.height==0&&slot.facing==-2&&slot.resolution==16;
    return door_range||house07_global_range||in_front_relative;
}
bool supported_child_shy_sitting_fallback_profile(const SlotTable& table,std::size_t type3_index,
    const SlotRecord& slot) {
    return table.version==10&&type3_index==10&&slot.type==3&&slot.flags==0x8055&&
        slot.offset_bits==std::array<std::uint32_t,3>{0,0,0}&&
        slot.proximity==std::optional<std::array<std::int32_t,3>>{{32,80,32}}&&
        slot.standing==1&&slot.sitting==10&&slot.ground==0&&slot.snap_target_slot==-1&&
        slot.max_size==0&&slot.unknown_i10==0&&slot.gradient_bits==0x3E400000&&
        slot.height==0&&slot.facing==-2&&slot.resolution==16;
}
std::int32_t floor_tile(std::int32_t coordinate) {
    auto quotient=coordinate/16;
    if(coordinate<0 && coordinate%16) --quotient;
    return quotient;
}
}
std::vector<RoutePoint> enumerate_reference_slot_range_lattice(
    const SlotTable& table,std::size_t type3_index,std::size_t candidate_limit) {
    const auto* slot=slot_of_type(table,3,type3_index);
    if(table.version!=10 || !slot || !slot->proximity || !slot->resolution)
        throw std::invalid_argument("reference SLOT lattice requires a version-10 type-3 slot with range fields");
    const auto min_proximity=(*slot->proximity)[0];
    const auto raw_max_proximity=(*slot->proximity)[1];
    const auto max_proximity=raw_max_proximity==0?min_proximity:raw_max_proximity;
    const auto resolution=*slot->resolution;
    if(min_proximity<0 || max_proximity<0 || max_proximity<min_proximity || resolution<=0 ||
       max_proximity==min_proximity)
        throw std::invalid_argument("reference SLOT lattice requires a valid ranged proximity profile");
    if(candidate_limit==0) throw std::invalid_argument("reference SLOT lattice candidate budget is zero");

    // VMSlotParser computes the last lattice coordinate by truncating the
    // maximum proximity to a whole resolution step, then visits x outer/y
    // inner, both inclusive. Distance, room and SLOT flags are applied later.
    const auto bound=(static_cast<std::int64_t>(max_proximity)/resolution)*resolution;
    const auto steps=static_cast<std::uint64_t>(bound/resolution);
    const auto axis=steps*2U+1U;
    if(axis>candidate_limit || axis>candidate_limit/axis)
        throw std::length_error("reference SLOT lattice exceeds candidate budget");
    const auto count=static_cast<std::size_t>(axis*axis);
    std::vector<RoutePoint> offsets;
    offsets.reserve(count);
    for(std::uint64_t x_index=0;x_index<axis;++x_index) {
        const auto x=-bound+static_cast<std::int64_t>(x_index)*resolution;
        for(std::uint64_t y_index=0;y_index<axis;++y_index) {
            const auto y=-bound+static_cast<std::int64_t>(y_index)*resolution;
            if(x<std::numeric_limits<std::int32_t>::min() || x>std::numeric_limits<std::int32_t>::max() ||
               y<std::numeric_limits<std::int32_t>::min() || y>std::numeric_limits<std::int32_t>::max())
                throw std::length_error("reference SLOT lattice coordinate exceeds int32");
            offsets.push_back({static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)});
        }
    }
    return offsets;
}
std::vector<RoutePoint> filter_reference_slot_range_proximity(
    const SlotTable& table,std::size_t type3_index,const std::vector<RoutePoint>& offsets) {
    const auto* slot=slot_of_type(table,3,type3_index);
    if(table.version!=10 || !slot || !slot->proximity || !slot->resolution || *slot->resolution<=0)
        throw std::invalid_argument("reference SLOT proximity filter requires a version-10 type-3 range slot");
    const auto min_proximity=(*slot->proximity)[0];
    const auto raw_max_proximity=(*slot->proximity)[1];
    const auto max_proximity=raw_max_proximity==0?min_proximity:raw_max_proximity;
    if(min_proximity<0 || max_proximity<0 || max_proximity<=min_proximity)
        throw std::invalid_argument("reference SLOT proximity filter requires a valid ranged proximity profile");

    constexpr std::uint32_t fso_square=1U<<30;
    const bool square=(slot->flags&fso_square)!=0;
    const auto max=static_cast<std::uint64_t>(max_proximity);
    const auto min=static_cast<std::uint64_t>(min_proximity);
    // For integer-coordinate offsets, sqrt(n) <= max+0.5 is exactly
    // n <= max^2+max; sqrt(n) >= min-0.5 is n >= min^2-min+1.
    const auto max_squared=max*max+max;
    const auto min_squared=min==0?0:min*min-min+1;
    std::vector<RoutePoint> candidates;
    candidates.reserve(offsets.size());
    for(const auto point:offsets) {
        const auto ax=point.x<0?static_cast<std::uint64_t>(-static_cast<std::int64_t>(point.x)):
                                static_cast<std::uint64_t>(point.x);
        const auto ay=point.y<0?static_cast<std::uint64_t>(-static_cast<std::int64_t>(point.y)):
                                static_cast<std::uint64_t>(point.y);
        std::uint64_t distance_squared=0;
        if(square) distance_squared=std::max(ax,ay)*std::max(ax,ay);
        else {
            // Values beyond max+1 cannot survive the upper band. Reject them
            // before squaring so the remaining unsigned sum cannot overflow.
            if(ax>max+1 || ay>max+1) continue;
            distance_squared=ax*ax+ay*ay;
        }
        if(distance_squared>=min_squared && distance_squared<=max_squared)
            candidates.push_back(point);
    }
    return candidates;
}
std::uint8_t reference_slot_entry_flags(
    RoutePoint object_position,std::uint8_t object_direction,RoutePoint candidate_position,
    std::uint8_t slot_direction_flags) {
    constexpr double pi=3.141592653589793238462643383279502884;
    constexpr double angle_error=-0.1;
    const auto dx=floor_tile(candidate_position.x)-floor_tile(object_position.x);
    const auto neg_dy=floor_tile(object_position.y)-floor_tile(candidate_position.y);
    auto degrees=std::atan2(static_cast<double>(dx),static_cast<double>(neg_dy))*(180.0/pi)-
                 static_cast<double>(object_direction%8)*45.0;
    degrees=std::fmod(std::fmod(degrees,360.0)+360.0,360.0);
    if(degrees>180.0) degrees-=360.0;
    std::uint8_t directions=0;
    if(degrees>=-45.0-angle_error && degrees<=45.0+angle_error) directions|=1U<<0;
    if(degrees>=0.0-angle_error && degrees<=90.0+angle_error) directions|=1U<<1;
    if(degrees>=45.0-angle_error && degrees<=135.0+angle_error) directions|=1U<<2;
    if((degrees>=90.0-angle_error && degrees<=180.0+angle_error)||degrees<=-180.0+angle_error) directions|=1U<<3;
    if(degrees>=135.0-angle_error||degrees<=-135.0+angle_error) directions|=1U<<4;
    if((degrees>=-180.0-angle_error&&degrees<=-90.0+angle_error)||degrees>=180.0+angle_error) directions|=1U<<5;
    if(degrees>=-135.0-angle_error&&degrees<=-45.0+angle_error) directions|=1U<<6;
    if(degrees>=-90.0-angle_error&&degrees<=0.0+angle_error) directions|=1U<<7;
    return directions&slot_direction_flags;
}
std::vector<ReferenceSlotCircleCandidate> enumerate_reference_slot_circle_candidates(
    const SlotTable& table,std::size_t type3_index,RoutePoint object_position,std::uint8_t object_direction) {
    const auto* slot=slot_of_type(table,3,type3_index);
    if(table.version!=10 || !slot || !supported_circle_slot_profile(*slot))
        throw std::invalid_argument("reference SLOT circle requires a verified House07 or Door v10 type-3 profile");
    const auto radius=(*slot->proximity)[0];
    // The diagonal rounding is an ODH candidate-generation rule, not a claim
    // about the original executable's tie-breaking or destination scoring.
    const auto diagonal=static_cast<std::int32_t>((static_cast<std::int64_t>(radius)*46341+32768)/65536);
    std::vector<ReferenceSlotCircleCandidate> candidates;
    candidates.reserve(headings.size());
    for(std::size_t n=0;n<headings.size();++n) {
        const auto step=(n&1U)?diagonal:radius;
        const auto x=static_cast<std::int64_t>(object_position.x)+headings[n].x*step;
        const auto y=static_cast<std::int64_t>(object_position.y)+headings[n].y*step;
        if(x<std::numeric_limits<std::int32_t>::min() || x>std::numeric_limits<std::int32_t>::max() ||
           y<std::numeric_limits<std::int32_t>::min() || y>std::numeric_limits<std::int32_t>::max())
            throw std::length_error("reference SLOT circle candidate exceeds int32");
        const RoutePoint point{static_cast<std::int32_t>(x),static_cast<std::int32_t>(y)};
        candidates.push_back({point,reference_slot_entry_flags(object_position,object_direction,point,
            static_cast<std::uint8_t>(slot->flags))});
    }
    return candidates;
}
PreRankedSlotRoute find_pre_ranked_slot_route(const RouteGrid& grid,RoutePoint start,
    const std::vector<PreRankedSlotGoal>& goals,RouteLimits limits) {
    PreRankedSlotRoute result{{RouteStatus::invalid_input,{},0,0},std::nullopt};
    if(!grid.walkable(start) || goals.empty()) return result;
    if(goals.size()>limits.candidates) {
        result.path.status=RouteStatus::budget_exhausted;return result;
    }
    std::vector<RoutePoint> positions;
    positions.reserve(goals.size());
    for(std::size_t n=0;n<goals.size();++n) {
        const auto& goal=goals[n];
        if(!grid.contains(goal.position) || goal.entry_flags==0 || goal.facing>=8) {
            result.path={RouteStatus::invalid_input,{},0,0};return result;
        }
        if(goal.sitting_approach&&!goal.sitting_object) {
            result.path={RouteStatus::invalid_input,{},0,0};return result;
        }
        const auto route_position=goal.sitting_approach.value_or(goal.position);
        if(!grid.contains(route_position)) {
            result.path={RouteStatus::invalid_input,{},0,0};return result;
        }
        positions.push_back(route_position);
    }
    auto ranked=find_ranked_route(grid,start,positions,limits);
    result.path=std::move(ranked.path);
    result.selected=ranked.selected;
    return result;
}
bool Routing::bind(Vm& vm) {
    if(executing_ || (vm_ && vm_!=&vm)) return false;
    vm_=&vm;return true;
}
VmPrimitive Routing::primitive() {
    return [this](const auto& frame,const auto& instruction,const auto&,const VmDispatch& dispatch) {
        return request(frame,instruction,dispatch);
    };
}
std::optional<RouteFrameContext> Routing::active_frame_context() const {
    if(!job_) return {};
    return job_->frame;
}
std::optional<float> Routing::incoming_direction(float caller_radian_direction) const noexcept {
    if(!job_) return {};
    const auto* parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;
    return route_incoming_direction(job_->frame,parent,caller_radian_direction);
}
bool Routing::consume_portal_turn() noexcept {
    if(!job_) return false;
    auto* parent=suspended_jobs_.empty()?nullptr:&suspended_jobs_.back().frame;
    return consume_route_portal_turn(job_->frame,parent);
}
bool Routing::call_external_frame(const VmCall& call) {
    if(executing_) return false;
    return schedule_external_frame(call,false);
}
bool Routing::schedule_external_frame(const VmCall& call,bool sitting) {
    if(!vm_ || !job_ || job_->external_frame_active ||
       vm_->run_generation()!=job_->run_generation || vm_->status()!=VmStatus::waiting ||
       !vm_->pending() || vm_->pending()->ticket!=job_->ticket ||
       call.return_mode!=VmCallReturnMode::ordinary) return false;
    if(!vm_->call_pending(job_->ticket,call)) return false;
    job_->external_frame_active=true;
    job_->external_frame_sitting=sitting;
    job_->external_frame_owner=call.context.code_owner;
    job_->external_frame_call=call;
    job_->frame.state=RouteFrameState::room_portal;
    return true;
}
bool Routing::try_next_sitting_goal() {
    if(!job_||job_->fallback_goals.empty())return false;
    const auto* actor=world_.find(job_->actor);
    const auto* target=world_.find(job_->target);
    if(!actor||!target||actor->generation!=job_->actor_generation||
       target->generation!=job_->target_generation||!actor->position||!target->position||actor->attachment)
        return false;
    if(route_grid_refresh_&&!route_grid_refresh_(*actor->position))return false;
    actor=world_.find(job_->actor);target=world_.find(job_->target);
    if(!actor||!target||!actor->position||!target->position||actor->attachment||
       actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||
       (job_->target!=job_->actor&&!(*target->position==job_->target_position)))
        return false;
    const auto result=find_pre_ranked_slot_route(grid_,{actor->position->x,actor->position->y},
                                                 job_->fallback_goals,limits_);
    if(result.path.status!=RouteStatus::found||!result.selected)return false;
    const auto index=*result.selected;
    const auto selected=job_->fallback_goals[index];
    job_->fallback_goals.erase(job_->fallback_goals.begin()+static_cast<std::ptrdiff_t>(index));
    job_->path=result.path.points;
    job_->next=1;
    job_->entry_flags=selected.entry_flags;
    job_->facing=selected.facing;
    job_->selected_sitting_object=selected.sitting_object;
    job_->expected=*actor->position;
    job_->expected_radian_direction=actor->radian_direction;
    job_->walking_started=true;
    job_->external_frame_sitting=false;
    job_->frame.state=job_->path.size()==1?RouteFrameState::turn_only:RouteFrameState::walking;
    diagnostic_.clear();
    return true;
}
PrimitiveOutcome Routing::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch) {
    const bool goto_relative=i.opcode==27;
    if(i.opcode!=45&&!goto_relative) return {};
    if(executing_) return unsupported("routing callback cannot reenter controller");
    if(!vm_ || dispatch.origin!=vm_) return unsupported("routing controller bound to another VM");
    bool nested_request=false;
    if(job_) {
        if(job_->run_generation!=vm_->run_generation()) {
            end_room_context(*job_);
            if(const auto* actor=world_.find(job_->actor);actor&&actor->generation==job_->actor_generation)
                world_.set_static_footprint(job_->actor,true);
            for(auto& parent:suspended_jobs_){
                end_room_context(parent);
                if(const auto* actor=world_.find(parent.actor);actor&&actor->generation==parent.actor_generation)
                    world_.set_static_footprint(parent.actor,true);
            }
            job_.reset();suspended_jobs_.clear();
        } else {
            const auto& stack=vm_->stack();
            const bool in_external_parent=job_->external_frame_active&&
                std::any_of(stack.begin(),stack.end(),[&](const auto& active_frame){
                    return active_frame.return_mode==VmCallReturnMode::external_frame_next_tick&&
                        active_frame.invocation_ticket==job_->ticket&&
                        active_frame.context.code_owner==job_->external_frame_owner;
                });
            if(!in_external_parent)
                return unsupported("routing request already active outside a portal BHAV child frame");
            if(suspended_jobs_.size()+1>=max_nested_jobs_)
                return unsupported("nested routing frame budget exhausted");
            nested_request=true;
        }
    }
    diagnostic_.clear();
    const bool saved_go_here_relative=goto_relative&&
        i.operands==std::array<std::uint8_t,8>{10,0,254,255,10,0,5,0};
    const bool bounded_goto_relative=goto_relative&&i.operands[0]==0&&i.operands[1]==0&&
        i.operands[2]==254&&(i.operands[3]==0||i.operands[3]==4)&&i.operands[4]==0&&
        i.operands[5]==0&&i.operands[6]==2&&i.operands[7]==0;
    // The real sit function (BHAV 8215) uses Location=InFrontOf (0),
    // Direction=Facing (-2), no route count, and default flags.
    const bool in_front_goto_relative=goto_relative&&
        i.operands==std::array<std::uint8_t,8>{0,0,0,254,0,0,0,0};
    const bool stair_goto_relative_shape=goto_relative&&i.operands[0]==0&&i.operands[1]==0&&
        i.operands[2]==254&&(i.operands[3]==0||i.operands[3]==4)&&i.operands[4]==0&&
        i.operands[5]==0&&i.operands[6]==0&&i.operands[7]==0;
    bool stair_goto_relative=false;
    if(stair_goto_relative_shape&&stair_goto_relative_) {
        struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
        stair_goto_relative=stair_goto_relative_(frame,i);
    }
    if(goto_relative&&!saved_go_here_relative&&!bounded_goto_relative&&
       !in_front_goto_relative&&!stair_goto_relative)
        return unsupported("GotoRelativePosition outside the synthetic route profiles or the exact saved Go Here profile");
    const auto data=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(i.operands[2]|(i.operands[3]<<8));
    if(!goto_relative && scope!=0&&scope!=1&&scope!=2)
        return unsupported("routing operand scope requires StackObject StackVariable/Literal or Global");
    if(!goto_relative && ((i.operands[4]&0xFEU)!=0 || i.operands[5] || i.operands[6] || i.operands[7]))
        return unsupported("routing operand flags/tail outside NoFailureTrees profile");
    SlotTable relative_slots{};
    const SlotTable* selected_slots=&globals_;
    if(in_front_goto_relative) {
        SlotRecord relative{};
        relative.type=3;relative.offset_bits={0,0,0};relative.standing=1;relative.sitting=0;
        relative.ground=0;relative.flags=0x01;relative.snap_target_slot=-1;
        relative.proximity=std::array<std::int32_t,3>{16,24,16};
        relative.max_size=100;relative.unknown_i10=0;relative.gradient_bits=0;
        relative.height=0;relative.facing=-2;relative.resolution=16;
        relative_slots={0,10,{relative},0};
        selected_slots=&relative_slots;
    }
    if(!goto_relative && (scope==0||scope==1)) {
        if(!stack_object_slots_)
            return unsupported("StackObject StackVariable/Literal SLOT table provider unavailable");
        struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
        selected_slots=stack_object_slots_(frame);
        if(!selected_slots)
            return unsupported("StackObject StackVariable/Literal SLOT table missing or ambiguous");
    }
    auto slot_index=static_cast<std::size_t>(data);
    if(!goto_relative && scope==0) {
        if(data>=frame.arguments.size())
            return unsupported("StackObject StackVariable SLOT argument unavailable");
        if(frame.arguments[data]<0)return {PrimitiveDisposition::goto_false,{}};
        slot_index=static_cast<std::size_t>(frame.arguments[data]);
    }
    const auto* slot=(goto_relative&&!in_front_goto_relative)?nullptr:slot_of_type(*selected_slots,3,slot_index);
    const bool child_shy_sitting_fallback=!goto_relative&&scope==2&&selected_slots==&globals_&&slot&&
        supported_child_shy_sitting_fallback_profile(*selected_slots,slot_index,*slot);
    const bool ranged_slot=slot&&selected_slots->version==10&&
        (supported_ranged_slot_profile(*slot)||child_shy_sitting_fallback);
    if(!goto_relative||in_front_goto_relative) {
        if(!slot) return {PrimitiveDisposition::goto_false,{}};
        // Door range routing has separate explicit scoring and host validation.
        if(selected_slots->version!=10 ||
           (!supported_circle_slot_profile(*slot)&&!ranged_slot))
            return unsupported("SLOT runtime profile not implemented");
        if(slot->sitting!=0&&!child_shy_sitting_fallback)
            return unsupported("sitting-weighted SLOT profile requires an exact supported fallback path");
        if(ranged_slot&&!ranged_slot_goals_)
            return unsupported("ranged SLOT requires an explicit scoring provider");
        if(child_shy_sitting_fallback&&!sitting_slot_candidate_preflight_)
            return unsupported("child-shy sitting SLOT requires a nearby-chair preflight");
        if(ranged_slot&&!goal_validator_)
            return unsupported("ranged SLOT requires a host room/clearance validator");
        if((slot->flags&0x0800U)!=0&&!goal_validator_)
            return unsupported("IgnoreRooms SLOT requires a host validator for the active single-room grid");
    }
    const auto* actor=world_.find(frame.context.caller);
    const auto* target=world_.find(frame.context.stack_object);
    const bool cross_floor_saved_go_here=goto_relative&&saved_go_here_relative&&room_pair_&&room_portal_step_;
    if(!actor || !target || !actor->position || !target->position || actor->kind!=EntityKind::avatar ||
       actor->person_data.size()<=37 || actor->attachment ||
       (actor->position->level!=target->position->level&&!cross_floor_saved_go_here))
        return unsupported("routing requires positioned free avatar and target on one level");
    if(nested_request && (actor->id!=job_->actor || actor->generation!=job_->actor_generation))
        return unsupported("nested Route45 changed the active avatar identity");
    if(child_shy_sitting_fallback) {
        if(target->kind!=EntityKind::avatar)
            return unsupported("child-shy sitting SLOT requires an avatar target");
        const auto actor_id=actor->id,target_id=target->id;
        const auto actor_generation=actor->generation,target_generation=target->generation;
        const auto actor_position=*actor->position,target_position=*target->position;
        std::optional<std::string> preflight_diagnostic;
        {
            struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
            preflight_diagnostic=sitting_slot_candidate_preflight_(*selected_slots,slot_index,
                actor_position,target_position);
        }
        actor=world_.find(actor_id);target=world_.find(target_id);
        if(!actor||!target||actor->generation!=actor_generation||target->generation!=target_generation||
           !actor->position||!target->position||!(*actor->position==actor_position)||
           !(*target->position==target_position)||actor->attachment)
            return unsupported("entity changed during child-shy sitting SLOT preflight");
        if(preflight_diagnostic)return unsupported(*preflight_diagnostic);
    }
    if((!goto_relative||saved_go_here_relative)&&room_pair_) {
        const auto actor_id=actor->id,target_id=target->id;
        const auto actor_generation=actor->generation,target_generation=target->generation;
        const auto actor_position=*actor->position,target_position=*target->position;
        const auto actor_direction=actor->radian_direction;
        const auto rooms=[&] {
            struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
            return room_pair_(actor_position,target_position);
        }();
        actor=world_.find(actor_id);target=world_.find(target_id);
        if(!actor||!target||actor->generation!=actor_generation||target->generation!=target_generation||
           !actor->position||!target->position||!(*actor->position==actor_position)||
           !(*target->position==target_position)||actor->radian_direction!=actor_direction||actor->attachment)
            return unsupported("entity changed during Route45 room resolver");
        if(!rooms)return unsupported("Route45 room resolver could not classify actor and target");
        const bool ignore_rooms=!goto_relative&&(slot->flags&0x0800U)!=0;
        const bool floor_transition=cross_floor_saved_go_here&&
            actor->position->level!=target->position->level;
        if((rooms->first!=rooms->second||floor_transition)&&!ignore_rooms) {
            if(!room_portal_step_)
                return unsupported("cross-room Route45 requires a bounded Portal15 step provider");
            RouteFrameContext frame_context;frame_context.state=RouteFrameState::room_portal;
            if(nested_request)suspended_jobs_.push_back(std::move(*job_));
            job_=Job{dispatch.ticket,actor->generation,target->generation,actor->id,target->id,
                     *actor->position,*target->position,actor->radian_direction,frame_context,
                     {},1,0,0,false,false,0,false,0,{},0,false,true,frame,i,0,true};
            job_->run_generation=vm_->run_generation();
            job_->deferred_room_route=true;
            job_->room_context_open=true;
            job_->replay_frame=frame;
            job_->replay_instruction=i;
            return {PrimitiveDisposition::wait,{}};
        }
    }
    const RoutePoint start{actor->position->x,actor->position->y};
    const auto allow_goal=[&](RoutePoint point) {
        if(!goal_validator_)return true;
        struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
        return goal_validator_(frame,point);
    };
    const auto fail_route=[&](std::int16_t code,const std::string& reason) -> PrimitiveOutcome {
        diagnostic_=reason;
        if(stair_goto_relative)
            return unsupported("stair GotoRelativePosition failure tree is not implemented: "+reason);
        if(!saved_go_here_relative||nested_request)
            return {PrimitiveDisposition::goto_false,reason};
        Job failed{};
        failed.ticket=dispatch.ticket;
        failed.actor_generation=actor->generation;failed.target_generation=target->generation;
        failed.actor=actor->id;failed.target=target->id;
        failed.expected=*actor->position;failed.target_position=*target->position;
        failed.expected_radian_direction=actor->radian_direction;
        failed.run_generation=vm_->run_generation();
        failed.call_failure_trees=true;failed.failure_pending=true;
        failed.failure_code=code;failed.failure_reason=reason;
        job_=std::move(failed);
        return {PrimitiveDisposition::wait,reason};
    };
    RoutePath path{RouteStatus::invalid_input,{},0,0};
    std::optional<PreRankedSlotGoal> selected;
    std::vector<PreRankedSlotGoal> fallback_goals;
    std::string invalid_route_input_diagnostic;
    std::size_t route_goal_count=0;
    if(goto_relative&&!in_front_goto_relative) {
        if(!goal_validator_)
            return unsupported("GotoRelativePosition exact-point goal requires a host bounds/room/clearance validator");
        const RoutePoint goal{target->position->x,target->position->y};
        if(!grid_.contains(goal))
            return fail_route(7,"GotoRelativePosition target lies outside the active route grid");
        if(!allow_goal(goal))
            return fail_route(7,"GotoRelativePosition target rejected by host room/clearance validator");
        route_goal_count=1;
        if(start==goal) {
            if(!grid_.walkable(start)) path={RouteStatus::no_path,{},0,0};
            else path={RouteStatus::found,{start},0,0};
        } else path=find_route(grid_,start,{goal},limits_);
    } else if((ranged_slot&&ranged_slot_goals_)||(!ranged_slot&&ranked_goals_)) {
        const auto actor_id=actor->id,target_id=target->id;
        const auto actor_generation=actor->generation,target_generation=target->generation;
        const auto actor_position=*actor->position,target_position=*target->position;
        const auto actor_radian_direction=actor->radian_direction;
        const auto candidates=[&] {
            struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
            const auto& provider=ranged_slot?ranged_slot_goals_:ranked_goals_;
            return provider(*selected_slots,slot_index,actor_position,target_position);
        }();
        actor=world_.find(actor_id);target=world_.find(target_id);
        if(!actor || !target || actor->generation!=actor_generation || target->generation!=target_generation ||
           !actor->position || !target->position || !(*actor->position==actor_position) ||
           !(*target->position==target_position) ||
           actor->radian_direction!=actor_radian_direction || actor->attachment || actor->person_data.size()<=37)
            return unsupported("entity changed during pre-ranked SLOT goal provider");
        // The provider owns ranking and destination validation, but cannot
        // bypass this controller's supported direction mask/facing profile.
        for(const auto& candidate:candidates)
            if((candidate.entry_flags&static_cast<std::uint8_t>(~slot->flags))!=0 ||
               candidate.entry_flags!=reference_slot_entry_flags(
                   {target_position.x,target_position.y},target_position.direction,
                   candidate.position,static_cast<std::uint8_t>(slot->flags)))
                return unsupported("pre-ranked SLOT goal has unsupported entry flags");
        std::vector<PreRankedSlotGoal> accepted;
        accepted.reserve(candidates.size());
        for(const auto& candidate:candidates)
            if(allow_goal(candidate.sitting_approach.value_or(candidate.position)))accepted.push_back(candidate);
        route_goal_count=accepted.size();
        if(!grid_.walkable(start))
            invalid_route_input_diagnostic="pre-ranked SLOT route start is outside the walkable grid";
        else if(candidates.empty())
            invalid_route_input_diagnostic="pre-ranked SLOT goal provider returned no candidates";
        else if(accepted.empty())
            invalid_route_input_diagnostic="host rejected every pre-ranked SLOT goal as outside the walkable grid";
        else for(const auto& goal:accepted)if(!grid_.contains(goal.position)||
            (goal.sitting_approach&&!grid_.contains(*goal.sitting_approach))||
            goal.entry_flags==0||goal.facing>=8) {
            invalid_route_input_diagnostic="pre-ranked SLOT goal is outside the grid or has invalid facing flags";
            break;
        }
        auto result=find_pre_ranked_slot_route(grid_,start,accepted,limits_);
        if(result.selected) {
            selected=accepted[*result.selected];
            fallback_goals.reserve(accepted.size()-1);
            for(std::size_t n=0;n<accepted.size();++n)
                if(n!=*result.selected)fallback_goals.push_back(accepted[n]);
        }
        path=std::move(result.path);
    }else {
        std::vector<RoutePoint> goals;
        bool direction_compatible_candidate=false;
        for(const auto& candidate:enumerate_reference_slot_circle_candidates(*selected_slots,slot_index,
                {target->position->x,target->position->y},target->position->direction)) {
            if(candidate.entry_flags==0)continue;
            direction_compatible_candidate=true;
            if(allow_goal(candidate.position))goals.push_back(candidate.position);
        }
        route_goal_count=goals.size();
        if(goals.empty()) {
            if(direction_compatible_candidate) {
                diagnostic_="host goal validator rejected every direction-compatible SLOT candidate";
            } else {
                diagnostic_="SLOT direction mask rejects every candidate (type3 index "+
                    std::to_string(slot_index)+", flags "+std::to_string(slot->flags)+")";
            }
            diagnostic_+=" (target "+std::to_string(target->id)+" at "+
                std::to_string(target->position->x)+","+std::to_string(target->position->y)+","+
                std::to_string(target->position->level)+" direction "+
                std::to_string(target->position->direction)+")";
            return {PrimitiveDisposition::goto_false,{}};
        }
        if(std::find(goals.begin(),goals.end(),start)!=goals.end()) {
            // Validate clearance even for the turn-only shortcut.
            if(!grid_.contains(start)) path={RouteStatus::invalid_input,{},0,0};
            else if(!grid_.walkable(start)) path={RouteStatus::no_path,{},0,0};
            else path={RouteStatus::found,{start},0,0};
        }else path=find_route(grid_,start,goals,limits_);
    }
    if(path.status==RouteStatus::budget_exhausted) return unsupported("routing search budget exhausted");
    if(path.status==RouteStatus::invalid_input) {
        if(invalid_route_input_diagnostic.empty())invalid_route_input_diagnostic=
            "routing inputs invalid: start "+std::to_string(start.x)+","+std::to_string(start.y)+
            " walkable="+std::string(grid_.walkable(start)?"yes":"no")+
            ", accepted goals="+std::to_string(route_goal_count);
        return unsupported(invalid_route_input_diagnostic);
    }
    if(path.status==RouteStatus::no_path) return fail_route(3,"no path in supplied clearance grid");
    const auto goal=path.points.back();
    const std::int16_t entry_flags=(goto_relative&&!in_front_goto_relative)?0:static_cast<std::int16_t>(selected?selected->entry_flags:reference_slot_entry_flags(
        {target->position->x,target->position->y},target->position->direction,goal,
        static_cast<std::uint8_t>(slot->flags)));
    if((!goto_relative||in_front_goto_relative) && entry_flags==0) {diagnostic_="selected route goal no longer matches SLOT direction mask";
        return {PrimitiveDisposition::goto_false,{}};}
    // The TS1 direction operand is a relative facing offset from the target:
    // 0 keeps its direction and 4 faces the opposite way. Other values remain
    // unsupported until their authored placement behavior is modeled.
    const auto arrival_facing=(goto_relative&&!in_front_goto_relative)?
        (i.operands[3]==255?actor->position->direction:
         static_cast<std::uint8_t>((target->position->direction+i.operands[3])&7U)):
        (selected?selected->facing:heading(goal,{target->position->x,target->position->y}));
    RouteFrameContext frame_context;
    if(path.points.size()==1) frame_context.state=RouteFrameState::turn_only;
    if(nested_request) suspended_jobs_.push_back(std::move(*job_));
    job_=Job{dispatch.ticket,actor->generation,target->generation,actor->id,target->id,
             *actor->position,*target->position,actor->radian_direction,frame_context,
             std::move(path.points),1,entry_flags,
             arrival_facing,goto_relative?i.operands[3]==255:slot->facing==-3,
             false,0,false,0,{},0,false,false,{}, {},0,false};
    job_->selected_sitting_object=selected?selected->sitting_object:std::nullopt;
    job_->fallback_goals=std::move(fallback_goals);
    job_->run_generation=vm_->run_generation();
    job_->call_failure_trees=saved_go_here_relative&&!nested_request;
    return {PrimitiveDisposition::wait,{}};
}
void Routing::end_room_context(Job& job) {
    if(!job.room_context_open)return;
    job.room_context_open=false;
    if(room_portal_context_end_)room_portal_context_end_(job.ticket);
}
bool Routing::advance_room_route(const std::optional<VmCall>& previous_call,
                                 const std::optional<bool>& previous_result) {
    if(!job_||!job_->deferred_room_route||!room_portal_step_||!vm_||
       vm_->run_generation()!=job_->run_generation||vm_->status()!=VmStatus::waiting||
       !vm_->pending()||vm_->pending()->ticket!=job_->ticket)
        return job_?finish(false,"deferred room route lost its root VM ticket"):false;
    const auto* actor=world_.find(job_->actor);const auto* target=world_.find(job_->target);
    if(!actor||!target||actor->generation!=job_->actor_generation||
       target->generation!=job_->target_generation||!actor->position||!target->position||
       !(*target->position==job_->target_position)||actor->attachment)
        return finish(false,"deferred room route participants changed");
    const auto actor_position=*actor->position,target_position=*target->position;
    const auto actor_direction=actor->radian_direction;
    const auto step_index=job_->portal_steps;
    const auto plan=[&] {
        struct CallbackGuard {bool& flag;explicit CallbackGuard(bool& value):flag(value){flag=true;}~CallbackGuard(){flag=false;}} callback_guard(executing_);
        return room_portal_step_(job_->ticket,actor_position,target_position,previous_call,previous_result,step_index);
    }();
    actor=world_.find(job_->actor);target=world_.find(job_->target);
    if(!actor||!target||actor->generation!=job_->actor_generation||
       target->generation!=job_->target_generation||!actor->position||!target->position||
       !(*actor->position==actor_position)||!(*target->position==target_position)||
       actor->radian_direction!=actor_direction||actor->attachment)
        return finish(false,"Route45 portal planner changed route participants");
    if(plan.status==RoutePortalStepStatus::unavailable)
        return finish(false,plan.diagnostic.empty()?"no bounded room portal step is available":plan.diagnostic);
    if(plan.status==RoutePortalStepStatus::call_ready) {
        if(!plan.call||plan.call->return_mode!=VmCallReturnMode::ordinary||
           job_->portal_steps>=max_portal_retries_||!vm_->call_pending(job_->ticket,*plan.call))
            return finish(false,"room portal planner returned an invalid, over-budget, or unschedulable call");
        ++job_->portal_steps;
        job_->external_frame_active=true;
        job_->external_frame_owner=plan.call->context.code_owner;
        job_->external_frame_call=*plan.call;
        job_->expected=*actor->position;
        job_->expected_radian_direction=actor->radian_direction;
        job_->frame.state=RouteFrameState::room_portal;
        return true;
    }
    if(plan.call||!room_pair_||!job_->replay_frame||!job_->replay_instruction)
        return finish(false,"room portal planner reported arrival without a valid local Route45 request");
    const auto rooms=[&] {
        struct CallbackGuard {bool& flag;explicit CallbackGuard(bool& value):flag(value){flag=true;}~CallbackGuard(){flag=false;}} callback_guard(executing_);
        return room_pair_(actor_position,target_position);
    }();
    actor=world_.find(job_->actor);target=world_.find(job_->target);
    if(!rooms||rooms->first!=rooms->second||!actor||!target||
       actor->generation!=job_->actor_generation||target->generation!=job_->target_generation||
       !actor->position||!target->position||!(*actor->position==actor_position)||
       !(*target->position==target_position)||actor->radian_direction!=actor_direction||actor->attachment)
        return finish(false,"portal planner claimed destination before the actor reached the target room");

    auto deferred=std::move(*job_);
    job_.reset();
    end_room_context(deferred);
    const auto was_executing=executing_;
    executing_=false;
    PrimitiveOutcome outcome;
    try {
        outcome=request(*deferred.replay_frame,*deferred.replay_instruction,
                        VmDispatch{vm_,deferred.ticket});
    } catch(...) {
        executing_=was_executing;
        job_=deferred;
        return finish(false,"local Route45 SLOT planning raised an exception after room transition");
    }
    executing_=was_executing;
    if(outcome.disposition==PrimitiveDisposition::wait&&job_&&job_->ticket==deferred.ticket&&
       !job_->deferred_room_route)
        return true;
    const auto reason=diagnostic_.empty()?"local Route45 SLOT planning failed after room transition":diagnostic_;
    if(job_)job_.reset();
    job_=deferred;
    return finish(false,reason);
}
bool Routing::finish(bool result,std::string reason) {
    if(!result&&job_->call_failure_trees&&!job_->failure_tree_invoked) {
        const auto pending=vm_->pending();
        if(pending&&pending->ticket==job_->ticket) {
            const auto* actor=world_.find(job_->actor);
            if(actor&&actor->generation==job_->actor_generation)
                world_.set_static_footprint(job_->actor,true);
            VmCall failure{routine_key(398),pending->context,
                           {job_->failure_code,job_->failure_blocker,0,0}};
            if(vm_->call_pending(job_->ticket,failure)) {
                job_->failure_tree_invoked=true;
                job_->failure_reason=reason;
                job_->external_frame_active=true;
                job_->external_frame_owner=failure.context.code_owner;
                job_->external_frame_call=std::move(failure);
                job_->frame.state=RouteFrameState::room_portal;
                return true;
            }
        }
    }
    end_room_context(*job_);
    const auto ticket=job_->ticket;
    if(suspended_jobs_.empty()) {
        const auto* actor=world_.find(job_->actor);
        if(actor && actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,true);
        job_.reset();
        last_tick_.reset();
    } else {
        job_.reset();
        job_=std::move(suspended_jobs_.back());
        suspended_jobs_.pop_back();
        const auto* actor=world_.find(job_->actor);
        if(actor && actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,!job_->walking_started);
    }
    diagnostic_=reason;
    return vm_->complete(ticket,result);
}
bool Routing::interrupt() {
    if(executing_||!job_||job_->failure_pending||job_->failure_tree_invoked)return false;
    if(!suspended_jobs_.empty()){
        const auto parent=std::find_if(suspended_jobs_.rbegin(),suspended_jobs_.rend(),
            [](const Job& candidate){return candidate.call_failure_trees&&!candidate.failure_tree_invoked;});
        if(parent==suspended_jobs_.rend())return false;
        parent->interrupt_pending=true;
        job_->interrupt_pending=true;
        return true;
    }
    if(!job_->call_failure_trees)return false;
    // Simitone names fail code 4 Interrupted. Keep this isolated until the
    // original PC executable's cancellation code is verified independently.
    if(job_->external_frame_active){
        job_->interrupt_pending=true;
        return true;
    }
    job_->failure_code=4;
    return finish(false,"route interrupted by player");
}
void Routing::cancel_chain(const std::string& reason) {
    if(job_) {
        end_room_context(*job_);
        const auto* actor=world_.find(job_->actor);
        if(actor&&actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,true);
    }
    for(auto& parent:suspended_jobs_) {
        end_room_context(parent);
        const auto* actor=world_.find(parent.actor);
        if(actor&&actor->generation==parent.actor_generation)
            world_.set_static_footprint(parent.actor,true);
    }
    job_.reset();suspended_jobs_.clear();diagnostic_=reason;last_tick_.reset();
}
bool Routing::tick(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved) {
    if(executing_) throw std::logic_error("routing ticks must not reenter");
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
    if(last_tick_ && (now<=*last_tick_ || now-*last_tick_!=1)) return false;
    last_tick_=now;
    if(!vm_ || !job_) return false;
    if(job_->failure_pending) {
        job_->failure_pending=false;
        return finish(false,job_->failure_reason);
    }
    if(job_->interrupt_pending&&!job_->external_frame_active){
        job_->interrupt_pending=false;
        if(job_->call_failure_trees)job_->failure_code=4;
        return finish(false,"route interrupted by player");
    }
    if(job_->external_frame_active) {
        if(vm_->run_generation()!=job_->run_generation) {
            cancel_chain("route canceled by VM restart during external frame");return false;
        }
        if(const auto returned=vm_->external_frame_return()) {
            const auto result=*returned;
            if(result.ticket!=job_->ticket || result.run_generation!=job_->run_generation ||
               result.code_owner!=job_->external_frame_owner ||
               !vm_->resume_external_frame(job_->ticket)) {
                cancel_chain("external route frame returned with mismatched ownership");return false;
            }
            job_->external_frame_active=false;
            if(job_->failure_tree_invoked)
                return finish(false,job_->failure_reason);
            if(job_->interrupt_pending){
                job_->interrupt_pending=false;
                job_->failure_code=4;
                return finish(false,"route interrupted by player");
            }
            if(job_->external_frame_sitting) {
                job_->external_frame_sitting=false;
                if(result.result)return finish(true,{});
                auto* actor=world_.find(job_->actor);
                const auto* target=world_.find(job_->target);
                if(!actor||!target||actor->generation!=job_->actor_generation||
                   target->generation!=job_->target_generation||!actor->position||!target->position||
                   actor->attachment||!grid_.contains({actor->position->x,actor->position->y})||
                   !grid_.walkable({actor->position->x,actor->position->y})||
                   (job_->target!=job_->actor&&!(*target->position==job_->target_position)))
                    return finish(false,"failed EntryPoint26 changed or invalidated route participants");
                if(route_grid_refresh_&&!route_grid_refresh_(*actor->position))
                    return finish(false,"failed EntryPoint26 could not refresh the route grid");
                job_->failure_code=5; // Simitone VMRouteFailCode.CantSit.
                if(try_next_sitting_goal())return true;
                return finish(false,"EntryPoint26 returned false and no alternate SLOT goal remains");
            }
            job_->frame.state=RouteFrameState::room_portal;
            const auto* actor=world_.find(job_->actor);
            const auto* target=world_.find(job_->target);
            if(!actor||!target||actor->generation!=job_->actor_generation||
               target->generation!=job_->target_generation||!actor->position||!target->position||
               (job_->target!=job_->actor&&!(*target->position==job_->target_position))||
               actor->attachment)
                return finish(false,"portal frame changed or invalidated route participants");
            const auto refresh_after_external_move=[&](bool required)->bool {
                const auto actor_position=*actor->position;
                const auto target_position=*target->position;
                const auto actor_direction=actor->radian_direction;
                if(!route_grid_refresh_)return !required;
                if(!route_grid_refresh_(actor_position))return false;
                actor=world_.find(job_->actor);
                target=world_.find(job_->target);
                return actor&&target&&actor->generation==job_->actor_generation&&
                    target->generation==job_->target_generation&&actor->position&&target->position&&
                    *actor->position==actor_position&&*target->position==target_position&&
                    actor->radian_direction==actor_direction&&!actor->attachment;
            };
            if(job_->deferred_room_route) {
                const auto previous_call=job_->external_frame_call;
                if(!refresh_after_external_move(true))
                    return finish(false,"Portal15 returned but the active room grid could not be refreshed");
                job_->expected=*actor->position;
                job_->expected_radian_direction=actor->radian_direction;
                return advance_room_route(previous_call,result.result);
            }
            if(!result.result) {
                if(!portal_retry_||!job_->external_frame_call||
                   job_->portal_retries>=max_portal_retries_)
                    return finish(false,"external route frame returned false; no bounded portal retry is available");
                if(!(*actor->position==job_->expected)) {
                    if(!refresh_after_external_move(true))
                        return finish(false,"failed portal frame moved avatar but host route-grid refresh is unavailable or failed");
                    job_->expected=*actor->position;
                    job_->expected_radian_direction=actor->radian_direction;
                    job_->portal_replan_required=true;
                }
                const auto failed_call=*job_->external_frame_call;
                const auto retry_index=job_->portal_retries+1;
                const auto actor_position=*actor->position;
                const auto target_position=*target->position;
                const auto next=[&] {
                    struct CallbackGuard {bool& flag;explicit CallbackGuard(bool& value):flag(value){flag=true;}~CallbackGuard(){flag=false;}} callback_guard(executing_);
                    return portal_retry_(failed_call,actor_position,target_position,retry_index);
                }();
                const auto* current_actor=world_.find(job_->actor);
                const auto* current_target=world_.find(job_->target);
                if(!current_actor||!current_target||current_actor->generation!=job_->actor_generation||
                   current_target->generation!=job_->target_generation||!current_actor->position||!current_target->position||
                   !(*current_actor->position==job_->expected)||
                   (job_->target!=job_->actor&&!(*current_target->position==job_->target_position))||
                   current_actor->attachment)
                    return finish(false,"portal retry provider changed or invalidated route participants");
                if(!next) return finish(false,"external route frame returned false; no alternate room route remains");
                if(next->return_mode!=VmCallReturnMode::ordinary||!vm_->call_pending(job_->ticket,*next))
                    return finish(false,"portal retry provider returned an invalid or unschedulable VM call");
                job_->portal_retries=retry_index;
                job_->external_frame_owner=next->context.code_owner;
                job_->external_frame_call=*next;
                job_->external_frame_active=true;
                job_->frame.state=RouteFrameState::room_portal;
                return true;
            }
            if(!(*actor->position==job_->expected)||job_->portal_replan_required) {
                if(!(*actor->position==job_->expected)&&!refresh_after_external_move(false))
                    return finish(false,"portal frame moved avatar but host route-grid refresh failed");
                const auto goal=job_->path.back();
                const auto replanned=find_route(grid_,{actor->position->x,actor->position->y},{goal},limits_);
                if(replanned.status!=RouteStatus::found)
                    return finish(false,"portal frame moved avatar but parent route could not be replanned");
                job_->path=replanned.points;job_->next=1;job_->expected=*actor->position;
                job_->expected_radian_direction=actor->radian_direction;
                job_->portal_replan_required=false;
            }
            // A portal BHAV resumes the suspended Route45 service only at this
            // scheduler boundary. Movement begins on a later tick.
            return true;
        }
        const auto& pending=vm_->pending();
        if((vm_->status()==VmStatus::waiting && pending && pending->ticket!=job_->ticket) ||
           vm_->status()==VmStatus::running || vm_->status()==VmStatus::budget_exhausted)
            return true; // Child BHAV is still executing or waiting on its own service.
        cancel_chain("external route frame ended without a matching return");return false;
    }
    if(!vm_->pending() || vm_->status()!=VmStatus::waiting || vm_->pending()->ticket!=job_->ticket) {
        cancel_chain("route canceled by VM restart or ticket mismatch");return false;
    }
    const auto* actor=world_.find(job_->actor);const auto* target=world_.find(job_->target);
    if(!actor || !target) return finish(false,"route canceled by missing participant");
    if(actor->generation!=job_->actor_generation) return finish(false,"route canceled by actor generation change");
    if(target->generation!=job_->target_generation) return finish(false,"route canceled by target generation change");
    if(!actor->position || !target->position) return finish(false,"route canceled by missing participant position");
    if(!(*actor->position==job_->expected))
        return finish(false,"route canceled by actor position change: actor="+std::to_string(job_->actor));
    if(target->id!=actor->id && !(*target->position==job_->target_position))
        return finish(false,"route canceled by target position change: target="+std::to_string(job_->target)+
            " expected="+std::to_string(job_->target_position.x)+","+std::to_string(job_->target_position.y)+
            " actual="+std::to_string(target->position->x)+","+std::to_string(target->position->y));
    if(actor->radian_direction!=job_->expected_radian_direction)
        return finish(false,"route canceled by continuous actor direction change");
    if(actor->attachment) return finish(false,"route canceled because actor became attached");
    if(job_->deferred_room_route)return advance_room_route({},{});
    std::optional<RouteMovement> event;
    if(job_->next<job_->path.size()) {
        const auto point=job_->path[job_->next];const RoutePoint from{actor->position->x,actor->position->y};
        if(!grid_.can_step(from,point)) return finish(false,"next segment blocked during route");
        if(!job_->walking_started) {
            if(!world_.set_static_footprint(actor->id,false))
                return finish(false,"dynamic route footprint update rejected");
            job_->walking_started=true;
        }
        job_->frame.state=RouteFrameState::walking;
        auto position=*actor->position;position.x=point.x;position.y=point.y;position.direction=heading(from,point);
        if(!world_.set_position(actor->id,position)) return finish(false,"position update rejected");
        job_->expected=position;
        job_->expected_radian_direction=world_.find(actor->id)->radian_direction;
        ++job_->next;
        event=RouteMovement{now,actor->id,position};
    }
    if(job_->next==job_->path.size()) {
        auto position=job_->expected;
        if(!job_->face_anywhere)position.direction=job_->facing;
        if(!world_.set_position(job_->actor,position) || !world_.set_person_data(job_->actor,37,job_->entry_flags))
            return finish(false,"route arrival fields rejected");
        constexpr float pi_over_four=0.78539816339744830962f;
        if(!job_->face_anywhere&&!world_.set_radian_direction(job_->actor,static_cast<float>(job_->facing)*pi_over_four))
            return finish(false,"route arrival facing rejected");
        if(event) event->position=position;
        if(job_->selected_sitting_object) {
            job_->failure_code=5; // Simitone VMRouteFailCode.CantSit.
            std::optional<VmCall> call;
            if(sitting_action_) {
                try {call=sitting_action_(job_->actor,*job_->selected_sitting_object);}
                catch(const std::exception& error) {diagnostic_=std::string("EntryPoint26 planning failed: ")+error.what();}
            }
            if(call&&schedule_external_frame(*call,true)) {
                if(event&&moved)moved(*event);
                return true;
            }
            if(call&&diagnostic_.empty())diagnostic_=std::string("EntryPoint26 VM call could not be scheduled: ")+vm_->diagnostic();
            else if(!call&&diagnostic_.empty())diagnostic_="EntryPoint26 provider returned no VM call";
            if(try_next_sitting_goal()) {
                if(event&&moved)moved(*event);
                return true;
            }
            return finish(false,diagnostic_.empty()?
                "selected sitting goal has no schedulable EntryPoint26 or alternate SLOT goal":diagnostic_);
        }
        const auto accepted=finish(true,{});
        if(event && moved) moved(*event);
        return accepted;
    }
    if(event && moved) moved(*event);
    return true;
}
} // namespace odh
