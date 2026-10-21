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
    std::size_t expanded=0;
    for(std::size_t n=0;n<goals.size();++n) {
        const auto& goal=goals[n];
        if(!grid.contains(goal.position) || goal.entry_flags==0 || goal.facing>=8) {
            result.path={RouteStatus::invalid_input,{},0,expanded};return result;
        }
        // Exhaustion cannot be interpreted as proof that this goal failed.
        if(expanded>=limits.expanded) {
            result.path={RouteStatus::budget_exhausted,{},0,expanded};return result;
        }
        auto path=find_route(grid,start,{goal.position},{limits.expanded-expanded,limits.frontier});
        expanded+=path.expanded;
        path.expanded=expanded;
        result.path=std::move(path);
        if(result.path.status==RouteStatus::found) {result.selected=n;return result;}
        if(result.path.status!=RouteStatus::no_path) return result;
    }
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
    if(executing_ || !vm_ || !job_ || job_->external_frame_active ||
       vm_->run_generation()!=job_->run_generation || vm_->status()!=VmStatus::waiting ||
       !vm_->pending() || vm_->pending()->ticket!=job_->ticket ||
       call.return_mode!=VmCallReturnMode::ordinary) return false;
    if(!vm_->call_pending(job_->ticket,call)) return false;
    job_->external_frame_active=true;
    job_->external_frame_owner=call.context.code_owner;
    job_->frame.state=RouteFrameState::room_portal;
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
            if(const auto* actor=world_.find(job_->actor);actor&&actor->generation==job_->actor_generation)
                world_.set_static_footprint(job_->actor,true);
            for(const auto& parent:suspended_jobs_)
                if(const auto* actor=world_.find(parent.actor);actor&&actor->generation==parent.actor_generation)
                    world_.set_static_footprint(parent.actor,true);
            job_.reset();suspended_jobs_.clear();
        } else {
            const auto& stack=vm_->stack();
            const bool in_external_parent=job_->external_frame_active&&!stack.empty()&&
                stack.back().return_mode==VmCallReturnMode::external_frame_next_tick&&
                stack.back().invocation_ticket==job_->ticket&&
                stack.back().context.caller==frame.context.caller&&
                stack.back().context.callee==frame.context.callee&&
                stack.back().context.stack_object==frame.context.stack_object&&
                stack.back().context.code_owner==frame.context.code_owner;
            if(!in_external_parent)
                return unsupported("routing request already active outside a portal BHAV child frame");
            if(suspended_jobs_.size()+1>=max_nested_jobs_)
                return unsupported("nested routing frame budget exhausted");
            nested_request=true;
        }
    }
    diagnostic_.clear();
    if(goto_relative && (i.operands[0]!=0 || i.operands[1]!=0 || i.operands[2]!=254 ||
        i.operands[3]!=0 || i.operands[4]!=0 || i.operands[5]!=0 ||
        i.operands[6]!=2 || i.operands[7]!=0))
        return unsupported("GotoRelativePosition outside the exact OnTopOf/SameDirection/NoFailureTrees profile");
    const auto data=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(i.operands[2]|(i.operands[3]<<8));
    if(!goto_relative && scope!=1&&scope!=2)
        return unsupported("routing operand scope requires StackObject Literal or Global");
    if(!goto_relative && ((i.operands[4]&0xFEU)!=0 || i.operands[5] || i.operands[6] || i.operands[7]))
        return unsupported("routing operand flags/tail outside NoFailureTrees profile");
    const SlotTable* selected_slots=&globals_;
    if(!goto_relative && scope==1) {
        if(!stack_object_slots_)
            return unsupported("StackObject Literal SLOT table provider unavailable");
        struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
        selected_slots=stack_object_slots_(frame);
        if(!selected_slots)
            return unsupported("StackObject Literal SLOT table missing or ambiguous");
    }
    const auto* slot=goto_relative?nullptr:slot_of_type(*selected_slots,3,data);
    if(!goto_relative) {
        if(!slot) return {PrimitiveDisposition::goto_false,{}};
        // Explicit first profile: circle in the same floor with zero offset. Other
        // SLOT rules must be implemented, not approximated as successful routing.
        if(selected_slots->version!=10 || !supported_circle_slot_profile(*slot))
            return unsupported("SLOT runtime profile not implemented");
        if((slot->flags&0x0800U)!=0&&!goal_validator_)
            return unsupported("IgnoreRooms SLOT requires a host validator for the active single-room grid");
    }
    const auto* actor=world_.find(frame.context.caller);
    const auto* target=world_.find(frame.context.stack_object);
    if(!actor || !target || !actor->position || !target->position || actor->kind!=EntityKind::avatar ||
       actor->person_data.size()<=37 || actor->attachment || actor->position->level!=target->position->level)
        return unsupported("routing requires positioned free avatar and target on one level");
    if(nested_request && (actor->id!=job_->actor || actor->generation!=job_->actor_generation))
        return unsupported("nested Route45 changed the active avatar identity");
    const RoutePoint start{actor->position->x,actor->position->y};
    const auto allow_goal=[&](RoutePoint point) {
        if(!goal_validator_)return true;
        struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
        return goal_validator_(frame,point);
    };
    RoutePath path{RouteStatus::invalid_input,{},0,0};
    std::optional<PreRankedSlotGoal> selected;
    if(goto_relative) {
        if(!goal_validator_)
            return unsupported("GotoRelativePosition exact-point goal requires a host bounds/room/clearance validator");
        const RoutePoint goal{target->position->x,target->position->y};
        if(!grid_.contains(goal) || !allow_goal(goal)) {
            diagnostic_="GotoRelativePosition exact StackObject point rejected by host validator";
            return {PrimitiveDisposition::goto_false,{}};
        }
        if(start==goal) {
            if(!grid_.walkable(start)) path={RouteStatus::no_path,{},0,0};
            else path={RouteStatus::found,{start},0,0};
        } else path=find_route(grid_,start,{goal},limits_);
    } else if(ranked_goals_) {
        const auto actor_id=actor->id,target_id=target->id;
        const auto actor_generation=actor->generation,target_generation=target->generation;
        const auto actor_position=*actor->position,target_position=*target->position;
        const auto actor_radian_direction=actor->radian_direction;
        const auto candidates=[&] {
            struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
            return ranked_goals_(*selected_slots,data,actor_position,target_position);
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
        for(const auto& candidate:candidates)if(allow_goal(candidate.position))accepted.push_back(candidate);
        auto result=find_pre_ranked_slot_route(grid_,start,accepted,limits_);
        if(result.selected) selected=accepted[*result.selected];
        path=std::move(result.path);
    }else {
        std::vector<RoutePoint> goals;
        for(const auto& candidate:enumerate_reference_slot_circle_candidates(*selected_slots,data,
                {target->position->x,target->position->y},target->position->direction))
            if(candidate.entry_flags!=0&&allow_goal(candidate.position)) goals.push_back(candidate.position);
        if(goals.empty()) {diagnostic_="SLOT direction mask rejects every candidate";return {PrimitiveDisposition::goto_false,{}};}
        if(std::find(goals.begin(),goals.end(),start)!=goals.end()) {
            // Validate clearance even for the turn-only shortcut.
            if(!grid_.contains(start)) path={RouteStatus::invalid_input,{},0,0};
            else if(!grid_.walkable(start)) path={RouteStatus::no_path,{},0,0};
            else path={RouteStatus::found,{start},0,0};
        }else path=find_route(grid_,start,goals,limits_);
    }
    if(path.status==RouteStatus::budget_exhausted) return unsupported("routing search budget exhausted");
    if(path.status==RouteStatus::invalid_input) return unsupported("routing start or pre-ranked goal input invalid");
    if(path.status==RouteStatus::no_path) {diagnostic_="no path in supplied clearance grid";return {PrimitiveDisposition::goto_false,{}};}
    const auto goal=path.points.back();
    const std::int16_t entry_flags=goto_relative?0:static_cast<std::int16_t>(selected?selected->entry_flags:reference_slot_entry_flags(
        {target->position->x,target->position->y},target->position->direction,goal,
        static_cast<std::uint8_t>(slot->flags)));
    if(!goto_relative && entry_flags==0) {diagnostic_="selected route goal no longer matches SLOT direction mask";
        return {PrimitiveDisposition::goto_false,{}};}
    const auto arrival_facing=goto_relative?target->position->direction:
        (selected?selected->facing:heading(goal,{target->position->x,target->position->y}));
    RouteFrameContext frame_context;
    if(path.points.size()==1) frame_context.state=RouteFrameState::turn_only;
    if(nested_request) suspended_jobs_.push_back(std::move(*job_));
    job_=Job{dispatch.ticket,actor->generation,target->generation,actor->id,target->id,
             *actor->position,*target->position,actor->radian_direction,frame_context,
             std::move(path.points),1,entry_flags,
             arrival_facing,!goto_relative&&slot->facing==-3};
    job_->run_generation=vm_->run_generation();
    return {PrimitiveDisposition::wait,{}};
}
bool Routing::finish(bool result,const std::string& reason) {
    const auto ticket=job_->ticket;
    if(suspended_jobs_.empty()) {
        const auto* actor=world_.find(job_->actor);
        if(actor && actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,true);
        job_.reset();
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
void Routing::cancel_chain(const std::string& reason) {
    if(job_) {
        const auto* actor=world_.find(job_->actor);
        if(actor&&actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,true);
    }
    for(const auto& parent:suspended_jobs_) {
        const auto* actor=world_.find(parent.actor);
        if(actor&&actor->generation==parent.actor_generation)
            world_.set_static_footprint(parent.actor,true);
    }
    job_.reset();suspended_jobs_.clear();diagnostic_=reason;
}
bool Routing::tick(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved) {
    if(executing_) throw std::logic_error("routing ticks must not reenter");
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
    if(last_tick_ && (now<=*last_tick_ || now-*last_tick_!=1)) return false;
    last_tick_=now;
    if(!vm_ || !job_) return false;
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
            job_->frame.state=RouteFrameState::room_portal;
            if(!result.result) return finish(false,"external route frame returned false; portal replanning unavailable");
            const auto* actor=world_.find(job_->actor);
            const auto* target=world_.find(job_->target);
            if(!actor||!target||actor->generation!=job_->actor_generation||
               target->generation!=job_->target_generation||!actor->position||!target->position||
               !(*target->position==job_->target_position)||actor->attachment)
                return finish(false,"portal frame changed or invalidated route participants");
            if(!(*actor->position==job_->expected)) {
                const auto goal=job_->path.back();
                const auto replanned=find_route(grid_,{actor->position->x,actor->position->y},{goal},limits_);
                if(replanned.status!=RouteStatus::found)
                    return finish(false,"portal frame moved avatar but parent route could not be replanned");
                job_->path=replanned.points;job_->next=1;job_->expected=*actor->position;
                job_->expected_radian_direction=actor->radian_direction;
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
    if(!actor || !target || actor->generation!=job_->actor_generation || target->generation!=job_->target_generation ||
       !actor->position || !target->position || !(*actor->position==job_->expected) ||
       !(*target->position==job_->target_position) ||
       actor->radian_direction!=job_->expected_radian_direction || actor->attachment)
        return finish(false,"route canceled by entity change");
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
        const auto accepted=finish(true,{});
        if(event && moved) moved(*event);
        return accepted;
    }
    if(event && moved) moved(*event);
    return true;
}
} // namespace odh
