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
    if(table.version!=10 || !slot || slot->offset_bits!=std::array<std::uint32_t,3>{0,0,0} ||
       slot->standing!=1 || slot->sitting!=0 || slot->ground!=0 || slot->flags!=0x55 ||
       slot->snap_target_slot!=-1 || slot->max_size!=0 || slot->unknown_i10!=0 ||
       slot->gradient_bits!=0x3E400000 || slot->height!=0 || slot->facing!=-2 ||
       !slot->proximity || slot->resolution!=16 ||
       (*slot->proximity)[0]<=0 || (*slot->proximity)[0]!=(*slot->proximity)[1] ||
       (*slot->proximity)[0]!=(*slot->proximity)[2] || (*slot->proximity)[0]>4096)
        throw std::invalid_argument("reference SLOT circle requires the verified House07 v10 type-3 profile");
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
PrimitiveOutcome Routing::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch) {
    if(i.opcode!=45) return {};
    if(executing_) return unsupported("routing callback cannot reenter controller");
    if(!vm_ || dispatch.origin!=vm_) return unsupported("routing controller bound to another VM");
    if(job_) {
        if(vm_->pending() && vm_->pending()->ticket==job_->ticket) return unsupported("routing request already active");
        const auto* actor=world_.find(job_->actor);
        if(actor && actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,true);
        job_.reset(); // A restarted VM invalidated the previous ticket.
    }
    diagnostic_.clear();
    const auto data=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
    const auto scope=static_cast<std::uint16_t>(i.operands[2]|(i.operands[3]<<8));
    if(scope!=2 || i.operands[4]!=0 || i.operands[5] || i.operands[6] || i.operands[7])
        return unsupported("routing operand profile requires global scope and zero flags/tail");
    const auto* slot=slot_of_type(globals_,3,data);
    if(!slot) return {PrimitiveDisposition::goto_false,{}};
    // Explicit first profile: circle in the same floor with zero offset. Other
    // SLOT rules must be implemented, not approximated as successful routing.
    if(globals_.version!=10 || slot->offset_bits!=std::array<std::uint32_t,3>{0,0,0} ||
       slot->flags!=0x55 || slot->standing!=1 || slot->sitting!=0 || slot->ground!=0 ||
       slot->snap_target_slot!=-1 || !slot->proximity || (*slot->proximity)[0]<=0 ||
       (*slot->proximity)[0]>4096 || (*slot->proximity)[0]!=(*slot->proximity)[1] ||
       (*slot->proximity)[0]!=(*slot->proximity)[2] || slot->height!=0 || slot->facing!=-2 ||
       slot->resolution!=16 || slot->max_size!=0 || slot->unknown_i10!=0 || slot->gradient_bits!=0x3E400000)
        return unsupported("SLOT runtime profile not implemented");
    const auto* actor=world_.find(frame.context.caller);
    const auto* target=world_.find(frame.context.stack_object);
    if(!actor || !target || !actor->position || !target->position || actor->kind!=EntityKind::avatar ||
       actor->person_data.size()<=37 || actor->attachment || actor->position->level!=target->position->level)
        return unsupported("routing requires positioned free avatar and target on one level");
    const RoutePoint start{actor->position->x,actor->position->y};
    RoutePath path{RouteStatus::invalid_input,{},0,0};
    std::optional<PreRankedSlotGoal> selected;
    if(ranked_goals_) {
        const auto actor_id=actor->id,target_id=target->id;
        const auto actor_generation=actor->generation,target_generation=target->generation;
        const auto actor_position=*actor->position,target_position=*target->position;
        const auto actor_radian_direction=actor->radian_direction;
        const auto candidates=[&] {
            struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
            return ranked_goals_(globals_,data,actor_position,target_position);
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
        auto result=find_pre_ranked_slot_route(grid_,start,candidates,limits_);
        if(result.selected) selected=candidates[*result.selected];
        path=std::move(result.path);
    }else {
        std::vector<RoutePoint> goals;
        for(const auto& candidate:enumerate_reference_slot_circle_candidates(globals_,data,
                {target->position->x,target->position->y},target->position->direction))
            if(candidate.entry_flags!=0) goals.push_back(candidate.position);
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
    const auto entry_flags=selected?selected->entry_flags:reference_slot_entry_flags({target->position->x,target->position->y},
        target->position->direction,goal,static_cast<std::uint8_t>(slot->flags));
    if(entry_flags==0) {diagnostic_="selected route goal no longer matches SLOT direction mask";
        return {PrimitiveDisposition::goto_false,{}};}
    RouteFrameContext frame_context;
    if(path.points.size()==1) frame_context.state=RouteFrameState::turn_only;
    job_=Job{dispatch.ticket,actor->generation,target->generation,actor->id,target->id,
             *actor->position,*target->position,actor->radian_direction,frame_context,
             std::move(path.points),1,entry_flags,
             selected?selected->facing:heading(goal,{target->position->x,target->position->y})};
    return {PrimitiveDisposition::wait,{}};
}
bool Routing::finish(bool result,const std::string& reason) {
    const auto ticket=job_->ticket;
    const auto* actor=world_.find(job_->actor);
    if(actor && actor->generation==job_->actor_generation)
        world_.set_static_footprint(job_->actor,true);
    job_.reset();diagnostic_=reason;
    return vm_->complete(ticket,result);
}
bool Routing::tick(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved) {
    if(executing_) throw std::logic_error("routing ticks must not reenter");
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
    if(last_tick_ && (now<=*last_tick_ || now-*last_tick_!=1)) return false;
    last_tick_=now;
    if(!vm_ || !job_) return false;
    if(!vm_->pending() || vm_->status()!=VmStatus::waiting || vm_->pending()->ticket!=job_->ticket) {
        const auto* actor=world_.find(job_->actor);
        if(actor && actor->generation==job_->actor_generation)
            world_.set_static_footprint(job_->actor,true);
        job_.reset();diagnostic_="route canceled by VM restart";return false;
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
        auto position=job_->expected;position.direction=job_->facing;
        if(!world_.set_position(job_->actor,position) || !world_.set_person_data(job_->actor,37,job_->entry_flags))
            return finish(false,"route arrival fields rejected");
        constexpr float pi_over_four=0.78539816339744830962f;
        if(!world_.set_radian_direction(job_->actor,static_cast<float>(job_->facing)*pi_over_four))
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
