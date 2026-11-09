#pragma once
#include "odh/session_bundle.hpp"
#include "odh/sha256.hpp"

namespace odh_host {
enum class HeadlessObjectCheckpointReason {
    healthy, inherited_frozen_fault, active_dispatch, budget_exhausted,
    missing_source_fault, source_was_not_faulted, identity_changed,
    resource_changed, source_fault_changed
};
inline const char* headless_object_checkpoint_reason(HeadlessObjectCheckpointReason value) {
    switch(value){
    case HeadlessObjectCheckpointReason::healthy:return "healthy";
    case HeadlessObjectCheckpointReason::inherited_frozen_fault:return "inherited_frozen_fault";
    case HeadlessObjectCheckpointReason::active_dispatch:return "active_dispatch";
    case HeadlessObjectCheckpointReason::budget_exhausted:return "budget_exhausted";
    case HeadlessObjectCheckpointReason::missing_source_fault:return "fault_has_no_authenticated_source_thread";
    case HeadlessObjectCheckpointReason::source_was_not_faulted:return "fault_is_new_since_source_checkpoint";
    case HeadlessObjectCheckpointReason::identity_changed:return "fault_identity_differs_from_source_generation";
    case HeadlessObjectCheckpointReason::resource_changed:return "fault_resource_identity_differs_from_source";
    case HeadlessObjectCheckpointReason::source_fault_changed:return "fault_VM_snapshot_differs_from_source";
    }
    return "invalid";
}
struct HeadlessCheckpointVmFingerprint {
    std::array<std::uint8_t,32> sha256{};
    std::size_t bytes=0;
    bool operator==(const HeadlessCheckpointVmFingerprint& other) const {return bytes==other.bytes&&sha256==other.sha256;}
    bool operator!=(const HeadlessCheckpointVmFingerprint& other) const {return !(*this==other);}
};
// Each encoder buffer is transient and subject to the same VM codec limits.
// Source and current buffers never coexist, and no bytes are retained in the
//32 metadata receipts or alongside the already owned source archive.
inline HeadlessCheckpointVmFingerprint headless_checkpoint_vm_fingerprint(
        const odh::VmSnapshot& snapshot,bool normalize_capture) {
    const auto fingerprint=[](const odh::VmSnapshot& value){
        const auto bytes=odh::encode_vm_snapshot(value);
        return HeadlessCheckpointVmFingerprint{odh::sha256(bytes.data(),bytes.size()),bytes.size()};
    };
    if(!normalize_capture||snapshot.action_tree_known)return fingerprint(snapshot);
    odh::SessionObjectThread captured;captured.vm=snapshot;
    odh::resolve_object_thread_action_tree(captured);
    return fingerprint(captured.vm);
}
struct HeadlessObjectCheckpointDecision {
    HeadlessObjectCheckpointReason reason=HeadlessObjectCheckpointReason::missing_source_fault;
    std::optional<HeadlessCheckpointVmFingerprint> current_fingerprint,source_fingerprint;
    bool admitted() const {return reason==HeadlessObjectCheckpointReason::healthy||
        reason==HeadlessObjectCheckpointReason::inherited_frozen_fault;}
};
// Value-only optional checkpoint policy. The source must come from the
// authenticated loaded archive, never from a live/latest observation. A frozen
// object fault gains no scheduling/recovery/write capability from admission.
inline HeadlessObjectCheckpointDecision headless_object_checkpoint_decision(
        const odh::SessionObjectThread& current,const odh::SessionObjectThread* source,bool dispatch_active) {
    using Reason=HeadlessObjectCheckpointReason;
    const auto answer=[](Reason reason){HeadlessObjectCheckpointDecision value;value.reason=reason;return value;};
    if(dispatch_active)return answer(Reason::active_dispatch);
    if(current.vm.status==odh::VmStatus::budget_exhausted)return answer(Reason::budget_exhausted);
    if(current.vm.status!=odh::VmStatus::fault)return answer(Reason::healthy);
    if(!source)return answer(Reason::missing_source_fault);
    if(current.id!=source->id||!current.generation||current.generation!=source->generation)
        return answer(Reason::identity_changed);
    if(!current.guid||current.guid!=source->guid||current.primary_owner!=source->primary_owner)
        return answer(Reason::resource_changed);
    if(source->vm.status!=odh::VmStatus::fault)return answer(Reason::source_was_not_faulted);
    // Match save_live_session's canonical object snapshot representation.
    // Only the owned copy's ancestry-known carrier is normalized; live VM
    // state, frames, registers, tickets and diagnostics are never rewritten.
    const auto before=headless_checkpoint_vm_fingerprint(source->vm,false);
    const auto now=headless_checkpoint_vm_fingerprint(current.vm,true);
    auto result=answer(now==before?Reason::inherited_frozen_fault:Reason::source_fault_changed);
    result.current_fingerprint=now;
    result.source_fingerprint=before;
    return result;
}
} // namespace odh_host
