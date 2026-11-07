// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/world.hpp"
#include "odh/vm.hpp"

namespace odh_host {
enum class SavedRecoveryExecutionPhase {load2,init0,cleanup3,restart_main,reset_execution,start_main,skipped4,placement9,remove10,wall6};
enum class SavedRecoveryArgumentMode {native_temp_defaults,explicit_four_zeros};
struct SavedRecoveryExecutionContext;
struct SavedRecoveryExecutionRequest;
struct SavedRecoveryExecutionResult;
// Issued only by a successful guarded object reset. It survives synchronous
// Cleanup but cannot authorize another World, body, Thread, owner or run.
class SavedRecoveryResetAuthority {
public:
    SavedRecoveryResetAuthority(const SavedRecoveryResetAuthority&)=delete;
    SavedRecoveryResetAuthority& operator=(const SavedRecoveryResetAuthority&)=delete;
    SavedRecoveryResetAuthority(SavedRecoveryResetAuthority&&)=delete;
    SavedRecoveryResetAuthority& operator=(SavedRecoveryResetAuthority&&)=delete;
    bool matches(const odh::World* world,odh::WorldEntityReference entity,const odh::Vm* thread,
            odh::CodeOwner owner,std::uint64_t run)const {
        return usable_&&!consumed_&&world_==world&&entity_==entity&&thread_==thread&&owner_==owner&&run_==run;
    }
private:
    SavedRecoveryResetAuthority(const odh::World* world,odh::WorldEntityReference entity,const odh::Vm* thread,
        odh::CodeOwner owner,std::uint64_t run):world_(world),entity_(entity),thread_(thread),owner_(owner),run_(run){}
    const odh::World* world_;odh::WorldEntityReference entity_;const odh::Vm* thread_;
    odh::CodeOwner owner_;std::uint64_t run_;bool usable_=false,consumed_=false;
    friend SavedRecoveryExecutionResult execute_saved_recovery_execution(
        const SavedRecoveryExecutionContext&,const SavedRecoveryExecutionRequest&);
};
struct SavedRecoveryExecutionRequest {
    SavedRecoveryExecutionPhase phase=SavedRecoveryExecutionPhase::load2;
    bool share_registers=false; // Explicit argument mode only; native defaults always share target registers.
    bool allow_lifecycle_deletion=false;
    std::uint64_t instruction_budget=10000;
    std::shared_ptr<SavedRecoveryResetAuthority> reset_authority; // Required by start_main only.
    SavedRecoveryArgumentMode argument_mode=SavedRecoveryArgumentMode::native_temp_defaults;
    odh::WorldEntityReference stack_object{}; // Exact live person required by skipped4.
};
struct SavedRecoveryExecutionContext {
    odh::World& world;
    odh::WorldEntityReference entity;
    odh::Vm* thread=nullptr; // Compared to reacquired authority BEFORE every dereference.
    odh::CodeOwner owner=0;
    std::function<bool()> authority;
    std::function<odh::Vm*(odh::WorldEntityReference)> thread_for;
    std::function<std::optional<odh::CodeOwner>(odh::WorldEntityReference)> owner_for;
    // Installs the real LifecycleScope around operation. Invoke exactly once,
    // synchronously, retain the actual Thread until evaluation/register loans
    // unwind. False cannot undo effects already completed by operation.
    std::function<bool(odh::Vm&,odh::WorldEntityReference,odh::CodeOwner,
        const std::function<bool()>& operation)> guarded_operation;
    odh::VmPrimitive synchronous_primitive;
    std::function<void(const odh::VmTrace&)> trace;
    // Exact original deletion/body authority, required when deletion is allowed.
    // A typed VM deletion stop alone never authenticates identity retirement.
    std::function<bool(odh::WorldEntityReference)> lifecycle_deleted;
    // Read-only proof from the actual host owners (playback, routing, waits,
    // dialogs and scheduler). VM-local idle state cannot prove their release.
    // Required for restart; checked again inside the guarded operation.
    std::function<bool(odh::WorldEntityReference,odh::Vm&,odh::CodeOwner)> external_owners_detached;
};
struct SavedRecoveryExecutionPreflight {
    bool supported=false,executable=false;
    std::uint16_t action=0;
    std::size_t argument_count=0;
    std::string diagnostic;
};
struct SavedRecoveryExecutionResult {
    bool completed=false,effects_started=false,main_started=false,no_action=false,lifecycle_deleted=false;
    std::uint16_t action=0;
    std::uint64_t instructions=0;
    odh::VmStatus vm_status=odh::VmStatus::ready;
    std::string diagnostic;
    std::shared_ptr<SavedRecoveryResetAuthority> reset_authority; // Published only for reset_execution.
};
// Read-only: no bank resolution/installation, stack reset, dispatch or queue
// mutation. Root banks must already belong to the actual authenticated Thread.
SavedRecoveryExecutionPreflight preflight_saved_recovery_execution(
    const SavedRecoveryExecutionContext&,const SavedRecoveryExecutionRequest&);
// Uses the existing Thread's native evaluate_entry_point_check (including its
// internal disposable check). No second entity/World or replacement Main VM.
// Coordinator owns phase order and queues/controllers; this kernel never clears
// them. Object restart requires actual external release before retiring VM
// bookkeeping; person restart additionally requires ordered unwind. Keeps TEMP/XL,
// starts Main with four exact zeros and executes no Main instruction.
SavedRecoveryExecutionResult execute_saved_recovery_execution(
    const SavedRecoveryExecutionContext&,const SavedRecoveryExecutionRequest&);
} // namespace odh_host
