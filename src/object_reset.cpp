#include "odh/object_reset.hpp"
#include <limits>
#include <map>
#include <memory>

namespace odh {
class VmSourceResetAccess {
public:
    static bool allowed(Vm& target,const VmInstructionLease* source){
        return source?target.source_reset_allowed(*source):
            !target.executing_&&!target.check_active_&&!target.external_check_active_;
    }
    static bool clear(Vm& target,const VmInstructionLease* source){return target.source_reset_clear(source);}
    static bool bind(Vm& target,VmContext& context){
        return target.bind_context_live(context)&&(!target.memory_.resolve_live_entity||
            (context.entity_references_bound&&context.caller_generation&&
             context.caller_generation==context.callee_generation));
    }
    static VmStatus main(Vm& target,RoutineKey key,VmContext context){return target.source_reset_main(key,context,{0,0,0,0});}
    static bool hook(Vm& target,const std::function<bool()>& operation){
        if(!operation)return true;
        ++target.source_reset_hook_depth_;
        struct Guard {unsigned& count;~Guard(){--count;}} guard{target.source_reset_hook_depth_};
        return operation();
    }
    static std::unique_ptr<VmInstructionChildScope> cleanup(Vm& check,Vm& target,
            const VmInstructionLease* source,std::function<bool()> current){
        return check.authorize_reset_cleanup(source,target,std::move(current));
    }
};
namespace {
struct ResetRecord {std::int16_t id;CodeOwner owner;std::shared_ptr<bool> alive;std::optional<VmInstructionLease> source;};
thread_local std::map<const Vm*,ResetRecord> active_resets;
thread_local unsigned reset_depth=0;
struct DepthGuard {DepthGuard(){++reset_depth;}~DepthGuard(){--reset_depth;}};
struct RecordGuard {
    const Vm* thread;std::shared_ptr<bool> alive;
    bool active=true;
    void release(){if(active){*alive=false;active_resets.erase(thread);active=false;}}
    ~RecordGuard(){release();}
};
ObjectResetResult reset_impl(const VmInstructionLease* source,Vm& check,Vm& thread,std::int16_t id,CodeOwner owner,
        const EntryPointTable& entries,std::uint64_t budget,const VmPrimitive& primitive,const ObjectResetHooks& hooks){
    ObjectResetResult result;
    const auto cleanup=entries.entries.size()>3?entries.entries[3].action:std::uint16_t{0};
    const auto main=entries.entries.size()>1?entries.entries[1].action:std::uint16_t{0};
    result.stages={{"cleanup",cleanup,VmStatus::ready,0,{}},{"main",main,VmStatus::ready,0,{}}};
    const auto fail=[&](const std::string& text,std::size_t stage=0){
        result.diagnostic=text;result.stages[stage].diagnostic=text;return result;};
    if(id<=0||!owner||&check==&thread)return fail("object reset requires positive identity, code owner and separate check");
    if(reset_depth>=16)return fail("source Reset recursion exceeds bounded lifecycle depth");
    if(!VmSourceResetAccess::allowed(thread,source))return fail("object reset lacks its actual safe point or source chain");
    DepthGuard depth;
    const auto existing=active_resets.find(&thread);
    if(existing!=active_resets.end()){
        if(!source||existing->second.id!=id||existing->second.owner!=owner||
           (existing->second.source&&!existing->second.source->valid()))
            return fail("source InReset identity/chain does not match active target");
        try{
            if(!VmSourceResetAccess::hook(thread,hooks.before_return_avatar_effects))
                return fail("source InReset avatar effects failed",1);
        }catch(const std::exception& error){return fail(std::string("source InReset avatar effects: ")+error.what(),1);}
        catch(...){return fail("source InReset avatar effects raised non-standard exception",1);}
        result.already_resetting=true;result.completed=true;return result;
    }
    if(check.dispatch_active())return fail("object reset requires a fresh cleanup evaluator");
    const bool cleanup_present=cleanup>255&&check.has_routine(routine_key(cleanup),owner);
    const bool main_present=main>255&&thread.has_routine(routine_key(main),owner);
    if(cleanup_present&&(!budget||check.status()!=VmStatus::ready||check.run_generation()||
        !check.stack().empty()||check.pending()))return fail("object reset requires fresh Cleanup and nonzero budget");
    if(thread.run_generation()>std::numeric_limits<std::uint64_t>::max()-(main_present?2U:1U))
        return fail("object reset run generation exhausted");
    // ExecuteEntryPoint retains the exact target entity, rather than resolving
    // a numeric ID again after arbitrary Cleanup effects.
    VmContext context{id,id,0,owner};
    try{if(!VmSourceResetAccess::bind(thread,context))return fail("source Reset target identity could not bind before effects");}
    catch(const std::exception& error){return fail(std::string("source Reset target identity: ")+error.what());}
    catch(...){return fail("source Reset target identity raised non-standard exception");}
    auto alive=std::make_shared<bool>(true);
    active_resets.emplace(&thread,ResetRecord{id,owner,alive,source?std::optional<VmInstructionLease>{*source}:std::nullopt});
    RecordGuard record{&thread,alive};
    try{
        if(!VmSourceResetAccess::clear(thread,source))return fail("object reset could not retain/clear entity execution");
        if(!VmSourceResetAccess::hook(thread,hooks.after_stack_cleared))return fail("source Reset target queue/waits detach failed");
        if(cleanup_present){
            const auto current=[weak=std::weak_ptr<bool>(alive)]{const auto value=weak.lock();return value&&*value;};
            auto child=VmSourceResetAccess::cleanup(check,thread,source,current);
            if(!child)return fail("source Reset Cleanup child authority rejected");
            if(check.start(routine_key(cleanup),context,{0,0,0,0})!=VmStatus::running){
                result.stages[0].status=check.status();return fail("object cleanup could not start");
            }
            const auto execution=check.run_synchronous_check_with_registers(thread,true,budget,primitive);
            result.steps=execution.steps;result.stages[0].steps=execution.steps;
            result.stages[0].status=execution.status;result.stages[0].diagnostic=execution.diagnostic;
            if(!execution.started||(execution.status!=VmStatus::returned_true&&execution.status!=VmStatus::returned_false))
                return fail("object cleanup did not return synchronously: "+execution.diagnostic);
        }else result.stages[0].diagnostic="Cleanup entry absent, ID<=255 or routine unavailable; skipped by reference";
        if(main_present){
            const auto status=VmSourceResetAccess::main(thread,routine_key(main),context);
            result.stages[1].status=status;
            if(status!=VmStatus::running)return fail("object Main could not start after cleanup",1);
            result.main_started=true;
        }else result.stages[1].diagnostic="Main entry absent, ID<=255 or routine unavailable; skipped by reference";
        // Base VMEntity.Reset clears InReset before VMAvatar's override effects.
        // A new Reset caused by that override therefore executes a new base Reset.
        record.release();
        if(!VmSourceResetAccess::hook(thread,hooks.before_return_avatar_effects))
            return fail("source Reset avatar effects failed",1);
        result.completed=true;return result;
    }catch(const std::exception& error){return fail(std::string("source Reset lifecycle: ")+error.what());}
    catch(...){return fail("source Reset lifecycle raised non-standard exception");}
}
}
ObjectResetResult run_object_reset(Vm& check,Vm& thread,std::int16_t id,CodeOwner owner,
        const EntryPointTable& entries,std::uint64_t budget,const VmPrimitive& primitive){
    return reset_impl(nullptr,check,thread,id,owner,entries,budget,primitive,{});
}
ObjectResetResult run_object_reset(const VmInstructionLease& source,Vm& check,Vm& thread,std::int16_t id,CodeOwner owner,
        const EntryPointTable& entries,std::uint64_t budget,const VmPrimitive& primitive,const ObjectResetHooks& hooks){
    return reset_impl(&source,check,thread,id,owner,entries,budget,primitive,hooks);
}
} // namespace odh
