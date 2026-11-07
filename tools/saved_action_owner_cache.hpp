#pragma once

#include "odh/seated_session_continuation.hpp"
#include "odh/vm.hpp"

namespace odh_host {

// A UI lookup cache may reuse an equivalent installed bank. Execution owners
// remain distinct: this predicate never changes a VM, provenance or snapshot.
inline bool equivalent_installed_action_owners(
        const odh::SessionResourceCodeOwner& a,
        const odh::SessionResourceCodeOwner& b,const odh::Vm& vm) {
    if(!a.owner||!b.owner||a.resource_guid!=b.resource_guid||a.source!=b.source||
       a.asset!=b.asset||a.definition_chunk_id!=b.definition_chunk_id||
       a.tree_table_id!=b.tree_table_id||a.root_routine!=b.root_routine||
       a.bank_kind!=b.bank_kind||a.identity!=b.identity||
       a.linked_semiglobal!=b.linked_semiglobal||a.routine_fingerprint!=b.routine_fingerprint)
        return false;
    const auto first=vm.shared_program_for_owner(a.owner);
    const auto second=vm.shared_program_for_owner(b.owner);
    const auto root=odh::session_resource_root_key(a);
    if(!first||!second||!first->routines().count(root)||!second->routines().count(root))return false;
    try {
        return odh::session_loaded_bank_fingerprint(first->routines())==a.routine_fingerprint&&
            odh::session_loaded_bank_fingerprint(second->routines())==b.routine_fingerprint;
    }catch(const odh::FormatError&){return false;}
}

} // namespace odh_host
