#include "odh/routing.hpp"
#include <algorithm>
#include <array>
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
PrimitiveOutcome Routing::request(const VmFrame& frame,const Instruction& i,const VmDispatch& dispatch) {
    if(i.opcode!=45) return {};
    if(executing_) return unsupported("routing callback cannot reenter controller");
    if(!vm_ || dispatch.origin!=vm_) return unsupported("routing controller bound to another VM");
    if(job_) {
        if(vm_->pending() && vm_->pending()->ticket==job_->ticket) return unsupported("routing request already active");
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
    const auto radius=(*slot->proximity)[0];
    // Fixed integer approximation for circle diagonals. Ties and scoring here
    // are ODH prototype rules; equivalence with original routing is unproven.
    const auto diagonal=static_cast<std::int32_t>((static_cast<std::int64_t>(radius)*46341+32768)/65536);
    std::vector<RoutePoint> goals;
    for(std::size_t n=0;n<8;++n) {
        const auto step=(n&1)?diagonal:radius;
        goals.push_back({target->position->x+headings[n].x*step,target->position->y+headings[n].y*step});
    }
    const RoutePoint start{actor->position->x,actor->position->y};
    RoutePath path{RouteStatus::invalid_input,{},0,0};
    if(std::find(goals.begin(),goals.end(),start)!=goals.end()) {
        // Simitone validates candidate SLOT positions before AttemptWalk can
        // take its TURN_ONLY shortcut. Require equivalent evidence from the
        // supplied clearance grid; coincidence alone does not prove a valid
        // destination.
        if(!grid_.contains(start)) path={RouteStatus::invalid_input,{},0,0};
        else if(!grid_.walkable(start)) path={RouteStatus::no_path,{},0,0};
        else path={RouteStatus::found,{start},0,0};
    }else path=find_route(grid_,start,goals);
    if(path.status==RouteStatus::budget_exhausted) return unsupported("routing search budget exhausted");
    if(path.status==RouteStatus::invalid_input) return unsupported("routing start outside walkable grid");
    if(path.status==RouteStatus::no_path) {diagnostic_="no path in supplied clearance grid";return {PrimitiveDisposition::goto_false,{}};}
    const auto goal=path.points.back();
    const auto absolute=heading({target->position->x,target->position->y},goal);
    const auto relative=(absolute+8-target->position->direction)%8;
    const auto mask=static_cast<std::int16_t>(((1U<<relative)|(1U<<((relative+7)%8))|
                                             (1U<<((relative+1)%8)))&slot->flags);
    job_=Job{dispatch.ticket,actor->generation,target->generation,actor->id,target->id,
             *actor->position,*target->position,std::move(path.points),1,mask,
             heading(goal,{target->position->x,target->position->y})};
    return {PrimitiveDisposition::wait,{}};
}
bool Routing::finish(bool result,const std::string& reason) {
    const auto ticket=job_->ticket;job_.reset();diagnostic_=reason;
    return vm_->complete(ticket,result);
}
bool Routing::tick(std::uint64_t now,const std::function<void(const RouteMovement&)>& moved) {
    if(executing_) throw std::logic_error("routing ticks must not reenter");
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
    if(last_tick_ && (now<=*last_tick_ || now-*last_tick_!=1)) return false;
    last_tick_=now;
    if(!vm_ || !job_) return false;
    if(!vm_->pending() || vm_->status()!=VmStatus::waiting || vm_->pending()->ticket!=job_->ticket) {
        job_.reset();diagnostic_="route canceled by VM restart";return false;
    }
    const auto* actor=world_.find(job_->actor);const auto* target=world_.find(job_->target);
    if(!actor || !target || actor->generation!=job_->actor_generation || target->generation!=job_->target_generation ||
       !actor->position || !target->position || !(*actor->position==job_->expected) ||
       !(*target->position==job_->target_position) || actor->attachment)
        return finish(false,"route canceled by entity change");
    std::optional<RouteMovement> event;
    if(job_->next<job_->path.size()) {
        const auto point=job_->path[job_->next];const RoutePoint from{actor->position->x,actor->position->y};
        if(!grid_.can_step(from,point)) return finish(false,"next segment blocked during route");
        auto position=*actor->position;position.x=point.x;position.y=point.y;position.direction=heading(from,point);
        if(!world_.set_position(actor->id,position)) return finish(false,"position update rejected");
        job_->expected=position;++job_->next;
        event=RouteMovement{now,actor->id,position};
    }
    if(job_->next==job_->path.size()) {
        auto position=job_->expected;position.direction=job_->facing;
        if(!world_.set_position(job_->actor,position) || !world_.set_person_data(job_->actor,37,job_->entry_flags))
            return finish(false,"route arrival fields rejected");
        if(event) event->position=position;
        const auto accepted=finish(true,{});
        if(event && moved) moved(*event);
        return accepted;
    }
    if(event && moved) moved(*event);
    return true;
}
} // namespace odh
