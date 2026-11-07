// SPDX-License-Identifier: MPL-2.0
#include "odh/object_lookup.hpp"

namespace odh {
ObjectLookupResult object_lookup_eligibility(const World& world,WorldEntityReference ref,const ObjectLookupServices& services){
    ObjectLookupResult result;
    const auto fail=[&](const char* why){result.diagnostic=why;return result;};
    try{
        if(!services.authority||!services.authority())return fail("object lookup source authority unavailable");
        const auto* body=world.resolve_reference(ref);
        if(!body||!body->definition)return fail("object lookup exact body/definition unavailable");
        const auto guid=body->definition->guid;
        VmFrame address;address.context.entity_references_bound=true;
        if(!vm_context_set_reference(address.context,VmEntityRole::stack_object,ref))return fail("object lookup reference is invalid");
        const auto independent=world.read(address,4,60);
        if(!independent)return fail("object lookup raw SimIndependent60 unavailable");
        WorldEntityReference leader=ref;
        if(services.groups){
            const auto resolved=services.groups->resolve_interaction_group(ref);
            if(!resolved){result.diagnostic="object lookup interaction group unavailable: "+services.groups->diagnostic();return result;}
            leader=*resolved;
        }else{
            const auto master=world.read(address,21,10);
            if(!master)return fail("object lookup definition MasterID unavailable");
            if(*master)return fail("object lookup native MT group owner unavailable");
        }
        if(!world.resolve_reference(leader))return fail("object lookup resolved interaction body disappeared");
        std::uint16_t flags=*independent?2U:0U;
        if(leader==ref){
            if(!services.table_for)return fail("object lookup TTAB authority unavailable");
            const auto resource=services.table_for(ref);
            if(resource.status==ObjectLookupTableStatus::unavailable)return fail("object lookup TTAB resource is unknown");
            if(resource.status==ObjectLookupTableStatus::absent){
                if(resource.table)return fail("object lookup absent TTAB carries contradictory table data");
            }else if(resource.status==ObjectLookupTableStatus::present){
                const auto* table=resource.table;
                if(!table||(table->status!=TtabStatus::decoded&&table->status!=TtabStatus::empty)||
                   table->declared_count!=table->interactions.size()||table->interactions.size()>4096||
                   (table->status==TtabStatus::empty&&!table->interactions.empty()))
                    return fail("object lookup TTAB is missing, unsupported or structurally inconsistent");
                //4359db uses signed JGE on the raw DWORD. Negative saved
                // thresholds (including80000000) are <100, not huge unsigned
                // values. No BHAV/check/motive/Category predicate is added.
                for(const auto& row:table->interactions)
                    if((row.autonomy_threshold<100U||row.autonomy_threshold>=0x80000000U)&&!(row.flags&0x80U)){
                        flags|=8U;break;
                    }
            }else return fail("object lookup TTAB status is invalid");
        }
        // All borrowed table reads have completed before final host checks.
        if(!services.authority())return fail("object lookup source authority expired during query");
        body=world.resolve_reference(ref);
        if(!body||!body->definition||body->definition->guid!=guid||
           !world.resolve_reference(leader)||world.read(address,4,60)!=independent)
            return fail("object lookup source identity or instance state changed during query");
        result.eligibility=ObjectLookupEligibility{ref,leader,flags};return result;
    }catch(const std::exception& error){result.diagnostic=std::string("object lookup resource query failed: ")+error.what();return result;}
}
} // namespace odh
