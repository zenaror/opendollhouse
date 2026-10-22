#include "odh/entity_scheduler.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace odh {

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
    if(!found->second.interrupt())return false;
    if(ticking_&&current_entity_&&entity>*current_entity_&&
       std::find(same_tick_ready_.begin(),same_tick_ready_.end(),entity)==same_tick_ready_.end())
        same_tick_ready_.push_back(entity);
    return true;
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
    entities_.erase(found);
    if(entities_.empty())budget_cursor_.reset();
    return true;
}

bool EntityVmScheduler::tick(std::uint64_t now,std::uint64_t instruction_budget,
                             std::uint64_t per_entity_budget,const Trace& trace) {
    if(faulted_)return false;
    if(ticking_||!instruction_budget||!per_entity_budget||
       now>std::numeric_limits<std::uint32_t>::max()||
       (!last_tick_&&now!=1)||(last_tick_&&(now<=*last_tick_||now-*last_tick_!=1))) {
        diagnostic_="scheduler tick, budget, or reentry outside profile";
        return false;
    }
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(ticking_);
    instructions_last_tick_=0;
    current_entity_.reset();same_tick_ready_.clear();
    try {
    std::vector<std::int16_t> order;
    order.reserve(entities_.size());
    for(const auto& entry:entities_)order.push_back(entry.first);
    if(order.empty()) {last_tick_=now;diagnostic_.clear();return true;}

    const auto fail=[&](const std::string& why){faulted_=true;diagnostic_=why;return false;};
    for(const auto id:order) {
        auto& entity=entities_.at(id);
        if(entity.before&&!entity.before(now))return fail("entity pre-tick controller rejected tick for "+std::to_string(id));
    }
    std::vector<std::int16_t> runnable;
    for(const auto id:order) {
        const auto status=entities_.at(id).vm->status();
        if(status==VmStatus::running||status==VmStatus::budget_exhausted)
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
        auto& entity=entities_.at(id);
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
                continue;
            }
            const auto before_steps=entity.vm->steps();
            const std::function<void(const VmTrace&)> entity_trace=trace?
                std::function<void(const VmTrace&)>{[&](const VmTrace& item){trace(id,item);}}:
                std::function<void(const VmTrace&)>{};
            current_entity_=id;
            struct CurrentGuard {std::optional<std::int16_t>& current;~CurrentGuard(){current.reset();}} current_guard{current_entity_};
            const auto result=entity.vm->run(slice,entity_trace);
            const auto after_steps=entity.vm->steps();
            if(after_steps<before_steps)return fail("entity VM run generation changed during scheduler tick");
            const auto consumed=after_steps-before_steps;
            if(consumed>slice||consumed>remaining)return fail("entity VM exceeded scheduler instruction budget");
            instructions_last_tick_+=consumed;remaining-=consumed;
            if(entity.vm->status()==VmStatus::fault)
                return fail("entity VM fault for "+std::to_string(id)+": "+entity.vm->diagnostic());
            (void)result;
        }
        if(entity.after&&!entity.after())return fail("entity post-run controller rejected tick for "+std::to_string(id));
    }
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
