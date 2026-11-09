#include "odh/entity_scheduler.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace odh {

EntityVmSchedulerSnapshot EntityVmScheduler::snapshot() const {
    if(ticking_)throw std::logic_error("scheduler snapshot during tick");
    EntityVmSchedulerSnapshot result;
    result.entities.reserve(entities_.size());
    for(const auto& [id,entity]:entities_)
        result.entities.push_back({id,entity.vm->run_generation(),entity.vm->steps(),entity.vm->status(),
                                   bool(entity.before),bool(entity.after),bool(entity.interrupt)});
    result.entity_limit=entity_limit_;
    result.budget_cursor=budget_cursor_;
    result.last_tick=last_tick_;
    result.instructions_last_tick=instructions_last_tick_;
    result.faulted=faulted_;
    result.diagnostic=diagnostic_;
    return result;
}

bool EntityVmScheduler::restore_snapshot(const EntityVmSchedulerSnapshot& state) {
    if(ticking_)throw std::logic_error("scheduler restore during tick");
    if(!fresh_||state.entity_limit>entity_limit_||state.entities.size()>state.entity_limit||
       state.entities.size()!=entities_.size()||
       (state.last_tick&&(!*state.last_tick||*state.last_tick>std::numeric_limits<std::uint32_t>::max()))||
       (state.instructions_last_tick&&!state.last_tick&&!state.faulted)||
       (state.faulted&&state.diagnostic.empty())||
       (state.budget_cursor&&!entities_.count(*state.budget_cursor)))return false;
    auto registered=entities_.begin();
    for(const auto& entity:state.entities) {
        const auto& bound=registered->second;
        if(entity.id!=registered->first||entity.run_generation!=bound.vm->run_generation()||
           entity.steps!=bound.vm->steps()||entity.status!=bound.vm->status()||
           entity.before!=bool(bound.before)||entity.after!=bool(bound.after)||
           entity.interrupt!=bool(bound.interrupt))return false;
        ++registered;
    }
    // The only potentially allocating owned value is copied before publishing.
    auto diagnostic=state.diagnostic;
    diagnostic_.swap(diagnostic);
    entity_limit_=state.entity_limit;
    budget_cursor_=state.budget_cursor;
    last_tick_=state.last_tick;
    instructions_last_tick_=state.instructions_last_tick;
    faulted_=state.faulted;
    fresh_=false;
    budget_yield_count_=0;first_budget_yield_.reset();
    return true;
}

bool EntityVmScheduler::add(std::int16_t entity,Vm& vm,BeforeRun before,AfterRun after,Interrupt interrupt) {
    if(ticking_||faulted_||!entity_limit_||entities_.size()>=entity_limit_||entities_.count(entity))
        return false;
    if(std::any_of(entities_.begin(),entities_.end(),[&](const auto& entry){return entry.second.vm==&vm;}))
        return false;
    entities_.emplace(entity,Entity{&vm,std::move(before),std::move(after),std::move(interrupt)});
    return true;
}

bool EntityVmScheduler::interrupt(std::int16_t entity) {
    if(faulted_)return false;
    const auto found=entities_.find(entity);
    if(found==entities_.end()||!found->second.interrupt)return false;
    fresh_=false;
    if(ticking_) {
        // A SimAntics thread may notify its own object while that object's
        // temporary named-tree call is still on the C++ stack. Defer controller
        // work until the sender VM has returned to a scheduler safe point.
        if(std::find(pending_interrupts_.begin(),pending_interrupts_.end(),entity)==pending_interrupts_.end())
            pending_interrupts_.push_back(entity);
        return true;
    }
    if(!found->second.interrupt())return false;
    return true;
}

void EntityVmScheduler::process_pending_interrupts() {
    if(pending_interrupts_.empty())return;
    std::vector<std::int16_t> pending;
    pending.swap(pending_interrupts_);
    for(const auto id:pending) {
        const auto found=entities_.find(id);
        if(found==entities_.end()||!found->second.interrupt||!found->second.interrupt())continue;
        if(current_entity_&&id>*current_entity_&&
           std::find(same_tick_ready_.begin(),same_tick_ready_.end(),id)==same_tick_ready_.end())
            same_tick_ready_.push_back(id);
    }
}

bool EntityVmScheduler::remove(std::int16_t entity) {
    if(ticking_||faulted_)return false;
    const auto found=entities_.find(entity);
    if(found==entities_.end())return false;
    if(budget_cursor_&&*budget_cursor_==entity) {
        auto successor=std::next(found);
        if(successor==entities_.end())successor=entities_.begin();
        if(successor==found)budget_cursor_.reset();
        else budget_cursor_=successor->first;
    }
    fresh_=false;
    entities_.erase(found);
    pending_interrupts_.erase(std::remove(pending_interrupts_.begin(),pending_interrupts_.end(),entity),pending_interrupts_.end());
    same_tick_ready_.erase(std::remove(same_tick_ready_.begin(),same_tick_ready_.end(),entity),same_tick_ready_.end());
    if(entities_.empty())budget_cursor_.reset();
    return true;
}

bool EntityVmScheduler::tick(std::uint64_t now,std::uint64_t instruction_budget,
                             std::uint64_t per_entity_budget,const Trace& trace,
                             const FaultRecovery& recover,const EntityTick& entity_tick,ControllerOrder controller_order) {
    if(faulted_)return false;
    fresh_=false;
    if(ticking_||!instruction_budget||!per_entity_budget||
       (controller_order!=ControllerOrder::all_before_dispatch && controller_order!=ControllerOrder::per_entity)||
       now>std::numeric_limits<std::uint32_t>::max()||
       (!last_tick_&&now!=1)||(last_tick_&&(now<=*last_tick_||now-*last_tick_!=1))) {
        diagnostic_="scheduler tick, budget, or reentry outside profile";
        return false;
    }
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(ticking_);
    instructions_last_tick_=0;
    current_entity_.reset();same_tick_ready_.clear();pending_interrupts_.clear();
    try {
    std::vector<std::int16_t> order;
    order.reserve(entities_.size());
    for(const auto& entry:entities_)order.push_back(entry.first);
    if(order.empty()) {last_tick_=now;diagnostic_.clear();return true;}

    const auto fail=[&](const std::string& why){faulted_=true;diagnostic_=why;return false;};
    struct EntityScope {
        std::optional<std::int16_t>& current;
        EntityScope(std::optional<std::int16_t>& value,std::int16_t id):current(value){current=id;}
        ~EntityScope(){current.reset();}
    };
    // Tick validation above guarantees a new boundary, including after a
    // restored checkpoint. Never release a primitive's next-tick stop within
    // the same run slice or initial unnumbered dispatch.
    for(const auto id:order) {
        auto& vm=*entities_.at(id).vm;
        if(vm.status()==VmStatus::next_tick && !vm.resume_next_tick())
            return fail("entity next-tick continuation could not resume for "+std::to_string(id));
    }
    if(controller_order==ControllerOrder::all_before_dispatch)for(const auto id:order) {
        EntityScope scope(current_entity_,id);
        auto& entity=entities_.at(id);
        if(entity.before&&!entity.before(now))return fail("entity pre-tick controller rejected tick for "+std::to_string(id));
        process_pending_interrupts();
    }
    process_pending_interrupts();
    std::vector<std::int16_t> runnable;
    for(const auto id:order) {
        const auto status=entities_.at(id).vm->status();
        if(controller_order==ControllerOrder::per_entity || status==VmStatus::running||status==VmStatus::budget_exhausted)
            runnable.push_back(id);
    }
    std::size_t start=0;
    if(budget_cursor_&&!runnable.empty()) {
        const auto next=std::lower_bound(runnable.begin(),runnable.end(),*budget_cursor_);
        if(next!=runnable.end())start=static_cast<std::size_t>(next-runnable.begin());
    }
    const auto grants=static_cast<std::size_t>(std::min<std::uint64_t>(runnable.size(),instruction_budget));
    std::vector<std::int16_t> funded;
    funded.reserve(grants);
    for(std::size_t offset=0;offset<grants;++offset)
        funded.push_back(runnable[(start+offset)%runnable.size()]);
    // Rotate scarce grants among VMs that can execute; preserve ascending
    // entity order for the actual shared-world updates.
    if(grants)budget_cursor_=runnable[(start+grants)%runnable.size()];
    std::sort(funded.begin(),funded.end());
    auto remaining=instruction_budget;
    std::size_t funded_remaining=grants;
    for(const auto id:order) {
        EntityScope scope(current_entity_,id);
        auto& entity=entities_.at(id);
        if(controller_order==ControllerOrder::per_entity && entity.before && !entity.before(now))
            return fail("entity pre-tick controller rejected tick for "+std::to_string(id));
        // Routing/controllers may notify another thread without executing a VM
        // instruction. Publish that interrupt at this safe point, while the
        // source entity identity still determines same-tick eligibility.
        process_pending_interrupts();
        bool ran_vm=false;
        bool after_observed=false;
        const bool has_grant=std::binary_search(funded.begin(),funded.end(),id);
        const auto allocation_slots=funded_remaining;
        if(has_grant) --funded_remaining;
        const auto status=entity.vm->status();
        const bool awakened_later=std::find(same_tick_ready_.begin(),same_tick_ready_.end(),id)!=same_tick_ready_.end();
        const bool runnable_now=status==VmStatus::running||status==VmStatus::budget_exhausted;
        if((has_grant||awakened_later)&&remaining&&runnable_now) {
            std::uint64_t slice=0;
            if(has_grant) {
                const auto fair_share=remaining/allocation_slots+(remaining%allocation_slots!=0);
                slice=std::min(per_entity_budget,fair_share);
            } else {
                const auto reserved=funded_remaining>remaining/per_entity_budget?
                    remaining:std::min(remaining,per_entity_budget*funded_remaining);
                slice=std::min(per_entity_budget,remaining-reserved);
            }
            if(!slice) {
                if(entity.after&&!entity.after())return fail("entity post-run controller rejected tick for "+std::to_string(id));
                process_pending_interrupts();
                if(entity_tick&&!entity_tick(id,now,false))return fail("entity lifecycle rejected tick for "+std::to_string(id));
                process_pending_interrupts();
                continue;
            }
            const std::function<void(const VmTrace&)> entity_trace=trace?
                std::function<void(const VmTrace&)>{[&](const VmTrace& item){trace(id,item);}}:
                std::function<void(const VmTrace&)>{};
            ran_vm=true;
            auto slice_remaining=slice;
            while(slice_remaining){
            after_observed=false;
            const auto result=entity.vm->run(slice_remaining,entity_trace);
            const auto consumed=entity.vm->last_run_instruction_count();
            if(result==VmStatus::budget_exhausted){
                if(budget_yield_count_!=std::numeric_limits<std::uint64_t>::max())++budget_yield_count_;
                if(!first_budget_yield_)first_budget_yield_=EntityVmSchedulerBudgetYield{
                    id,now,slice_remaining,consumed};
            }
            process_pending_interrupts();
            if(consumed>slice_remaining||consumed>remaining)return fail("entity VM exceeded scheduler instruction budget");
            instructions_last_tick_+=consumed;remaining-=consumed;
            slice_remaining-=consumed;
            if(entity.vm->status()==VmStatus::fault) {
                const auto fault=entity.vm->diagnostic();
                if(!recover || !recover(id,*entity.vm,now) || entity.vm->status()==VmStatus::fault) {
                    std::string location;
                    if(!entity.vm->stack().empty()) {
                        const auto& frame=entity.vm->stack().back();
                        location=" routine="+std::to_string(frame.key.id)+" pc="+std::to_string(frame.pc)+
                            " owner="+std::to_string(frame.context.code_owner);
                    }
                    return fail("entity VM fault for "+std::to_string(id)+location+": "+fault);
                }
            }
            if(result==VmStatus::yielded&&(entity.vm->immediate_return()||entity.vm->interaction_return())){
                if(entity.after&&!entity.after())return fail("entity return controller rejected tick for "+std::to_string(id));
                after_observed=true;
                process_pending_interrupts();
                const auto resumed=entity.vm->status();
                if(consumed&&slice_remaining&&(resumed==VmStatus::running||resumed==VmStatus::budget_exhausted))continue;
            }
            break;
            }
        }
        if(!after_observed&&entity.after&&!entity.after())return fail("entity post-run controller rejected tick for "+std::to_string(id));
        process_pending_interrupts();
        if(entity_tick&&!entity_tick(id,now,ran_vm))return fail("entity lifecycle rejected tick for "+std::to_string(id));
        process_pending_interrupts();
    }
    process_pending_interrupts();
    last_tick_=now;
    diagnostic_.clear();
    return true;
    } catch(const std::exception& error) {
        faulted_=true;
        diagnostic_=std::string("entity scheduler callback threw: ")+error.what();
        return false;
    } catch(...) {
        faulted_=true;
        diagnostic_="entity scheduler callback threw an unknown exception";
        return false;
    }
}

} // namespace odh
