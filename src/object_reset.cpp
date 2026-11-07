#include "odh/object_reset.hpp"
#include <limits>
#include <set>

namespace odh {
namespace {
// Mirrors VMEntity.InReset for nested callbacks on this execution thread.
thread_local std::set<const Vm*> active_resets;
struct ResetGuard {
    const Vm* thread;
    ~ResetGuard(){active_resets.erase(thread);}
};
}
ObjectResetResult run_object_reset(Vm& check,Vm& thread,std::int16_t id,CodeOwner owner,
    const EntryPointTable& entries,std::uint64_t budget,const VmPrimitive& primitive) {
    ObjectResetResult result;
    const auto cleanup=entries.entries.size()>3?entries.entries[3].action:std::uint16_t{0};
    const auto main=entries.entries.size()>1?entries.entries[1].action:std::uint16_t{0};
    result.stages={{"cleanup",cleanup,VmStatus::ready,0,{}},{"main",main,VmStatus::ready,0,{}}};
    const auto fail=[&](const std::string& text,std::size_t stage=0){
        result.diagnostic=text;result.stages[stage].diagnostic=text;return result;};
    if(id<=0 || !owner || &check==&thread)return fail("object reset requires positive identity, code owner and a separate check VM");
    if(check.dispatch_active() || thread.dispatch_active())return fail("object reset requires an external VM safe point");
    if(active_resets.count(&thread))return fail("object reset is already active for this entity VM");
    const bool cleanup_present=cleanup>255 && check.has_routine(routine_key(cleanup),owner);
    const bool main_present=main>255 && thread.has_routine(routine_key(main),owner);
    if(cleanup_present && (!budget || check.status()!=VmStatus::ready || check.run_generation()!=0 ||
        !check.stack().empty() || check.pending()))return fail("object reset requires a fresh cleanup check and nonzero budget");
    // Clearing execution and starting Main each retire a native run generation.
    if(thread.run_generation()>std::numeric_limits<std::uint64_t>::max()-(main_present?2U:1U))
        return fail("object reset run generation exhausted");
    active_resets.insert(&thread);ResetGuard guard{&thread};
    if(!thread.reset_execution_state())return fail("object reset could not clear entity execution");
    const VmContext context{id,id,0,owner};
    if(cleanup_present) {
        if(check.start(routine_key(cleanup),context,{0,0,0,0})!=VmStatus::running) {
            result.stages[0].status=check.status();return fail("object cleanup could not start");
        }
        const auto execution=check.run_synchronous_check_with_registers(thread,true,budget,primitive);
        result.steps=execution.steps;result.stages[0].steps=execution.steps;
        result.stages[0].status=execution.status;result.stages[0].diagnostic=execution.diagnostic;
        if(!execution.started || (execution.status!=VmStatus::returned_true && execution.status!=VmStatus::returned_false))
            return fail("object cleanup did not return synchronously: "+execution.diagnostic);
    } else result.stages[0].diagnostic="Cleanup entry absent, ID<=255 or routine unavailable; skipped by reference";
    if(main_present) {
        const auto temps=thread.temps();const auto xl=thread.temp_xl();
        const auto status=thread.start(routine_key(main),context,{0,0,0,0});
        thread.temps()=temps;thread.temp_xl()=xl;
        result.stages[1].status=status;
        if(status!=VmStatus::running)return fail("object Main could not start after cleanup",1);
        result.main_started=true;
    } else result.stages[1].diagnostic="Main entry absent, ID<=255 or routine unavailable; skipped by reference";
    result.completed=true;return result;
}
} // namespace odh
