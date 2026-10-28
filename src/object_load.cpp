#include "odh/object_load.hpp"

namespace odh {
SavedEntityLoadResult run_saved_entity_load(Vm& check,Vm& restored,std::int16_t id,
    CodeOwner owner,const EntryPointTable& entries,std::uint64_t budget,const VmPrimitive& primitive) {
    SavedEntityLoadResult result;
    const auto routine=entries.entries.size()>2?entries.entries[2].action:std::uint16_t{0};
    result.stages.push_back({"load",routine,VmStatus::ready,0,{}});
    const auto fail=[&](const char* reason){result.diagnostic=reason;result.stages[0].diagnostic=reason;return result;};
    if(id<=0 || !owner || &check==&restored)return fail("saved entity Load requires positive identity, code owner and a separate check VM");
    if(check.dispatch_active() || restored.dispatch_active())
        return fail("saved entity Load requires an external safe point outside VM dispatch");
    if(routine<=255) {
        result.stages[0].diagnostic="Load entry absent or ID<=255 skipped by reference";
        result.completed=true;return result;
    }
    if(!budget)return fail("saved entity Load requires a nonzero instruction budget");
    if(check.status()!=VmStatus::ready || check.run_generation()!=0 || !check.stack().empty() || check.pending())
        return fail("saved entity Load requires a fresh disposable check VM");
    if(!check.has_routine(routine_key(routine),owner))
        return fail("saved entity Load code is not installed under its exact resource owner");
    const VmContext context{id,id,0,owner};
    if(check.start(routine_key(routine),context,{0,0,0,0})!=VmStatus::running) {
        result.stages[0].status=check.status();return fail("saved entity Load could not start under its resource owner");
    }
    const auto execution=check.run_synchronous_check_with_registers(restored,false,budget,primitive);
    result.steps=execution.steps;result.stages[0].status=execution.status;result.stages[0].steps=execution.steps;
    result.stages[0].diagnostic=execution.diagnostic;
    result.completed=execution.started && (execution.status==VmStatus::returned_true || execution.status==VmStatus::returned_false);
    if(!result.completed) {
        result.diagnostic="saved entity Load did not return synchronously ("+std::string(vm_status_name(execution.status))+")";
        if(!execution.diagnostic.empty())result.diagnostic+=": "+execution.diagnostic;
    }
    return result;
}
} // namespace odh
