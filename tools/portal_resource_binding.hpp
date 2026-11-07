#pragma once

#include "odh/seated_session_continuation.hpp"
#include "odh/vm.hpp"

namespace odh_host {

// A Portal15 owner belongs to its immutable resource bank, not to the current
// physical part of a multi-OBJD portal. Both definitions and both independently
// loaded banks are required: matching an IFF filename alone grants no reuse.
inline void validate_portal_resource_binding(
        const odh::SessionResourceCodeOwner& manifest,
        const odh::DefinitionCandidate& seed,const odh::DefinitionCandidate& component,
        std::uint16_t seed_action,std::uint16_t component_action,
        const odh::SessionActionRoutineSource& seed_bank,
        const odh::SessionActionRoutineSource& component_bank,
        const odh::VmProgram& process_bank,const odh::Vm& resident) {
    if(!manifest.owner||manifest.resource_guid!=seed.definition.guid||
       manifest.source!=seed.source||manifest.asset!=seed.asset||
       manifest.definition_chunk_id!=seed.chunk_id||manifest.tree_table_id!=seed.definition.tree_table_id)
        throw odh::FormatError("portal resource owner no longer matches its exact seed definition");
    if(component.source!=manifest.source||component.asset!=manifest.asset||
       !seed_action||seed_action!=manifest.root_routine||component_action!=manifest.root_routine)
        throw odh::FormatError("portal component differs from its cached resource or Portal15 action");
    if(manifest.bank_kind!=odh::SessionCodeBankKind::private_closure&&
       manifest.bank_kind!=odh::SessionCodeBankKind::complete_private&&
       manifest.bank_kind!=odh::SessionCodeBankKind::private_and_semiglobal)
        throw odh::FormatError("portal resource owner has an unknown bank kind");
    if((manifest.bank_kind==odh::SessionCodeBankKind::private_and_semiglobal)!=manifest.linked_semiglobal.has_value()||
       seed_bank.linked_semiglobal!=manifest.linked_semiglobal||
       component_bank.linked_semiglobal!=manifest.linked_semiglobal)
        throw odh::FormatError("portal component GLOB differs from its exact resource bank");
    const auto installed=resident.shared_program_for_owner(manifest.owner);
    const auto root=odh::routine_key(component_action);
    if(!installed||!seed_bank.routines.count(root)||!component_bank.routines.count(root)||
       !process_bank.routines().count(root)||!installed->routines().count(root))
        throw odh::FormatError("portal action is absent from its exact resident resource bank");
    if(odh::session_loaded_bank_fingerprint(seed_bank.routines)!=manifest.routine_fingerprint||
       odh::session_loaded_bank_fingerprint(component_bank.routines)!=manifest.routine_fingerprint||
       odh::session_loaded_bank_fingerprint(process_bank.routines())!=manifest.routine_fingerprint||
       odh::session_loaded_bank_fingerprint(installed->routines())!=manifest.routine_fingerprint)
        throw odh::FormatError("portal component or installed resource bank fingerprint changed");
}

} // namespace odh_host
