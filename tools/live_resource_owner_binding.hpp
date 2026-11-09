// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/routing.hpp"
#include "odh/seated_session_continuation.hpp"

namespace odh_host {
namespace resource_owner_detail {
// The caller has selected and verified this logical owner against its immutable
// resource registry. Receiver/source body and registered-Thread identities are
// checked on both sides; neither callback grants an active admission by itself.
template<class Admit>
bool bind_registered(odh::Vm& receiver,odh::CodeOwner owner,
        const std::shared_ptr<const odh::VmProgram>& registered,
        const std::function<bool()>& identities,const Admit& admit,std::string* diagnostic){
    const auto reject=[&](const std::string& reason){if(diagnostic)*diagnostic=reason;return false;};
    if(diagnostic)diagnostic->clear();
    if(!owner||!registered||registered->routines().empty()||!identities||!identities())
        return reject("resource owner lacks its registered bank/source/recipient identity");
    try{
        if(!admit())return reject("resource owner source boundary is unavailable or expired");
        const auto installed=receiver.shared_program_for_owner(owner);
        if(!installed||(installed!=registered&&
           odh::session_loaded_bank_fingerprint(installed->routines())!=
           odh::session_loaded_bank_fingerprint(registered->routines())))
            return reject("resource owner differs from its exact registered immutable bank");
        if(!identities())return reject("resource owner source or recipient changed during admission");
        return true;
    }catch(const std::exception& error){return reject(std::string("resource owner admission: ")+error.what());}
}
}
// Actual executor may be a disposable authenticated source check; it is not
// necessarily the persistent entity Thread receiving the selected action.
inline bool bind_instruction_resource_owner(odh::Vm& receiver,odh::CodeOwner owner,
        const std::shared_ptr<const odh::VmProgram>& registered,
        const std::function<bool()>& identities,const odh::VmFrame& frame,
        const odh::Instruction& instruction,const odh::VmDispatch& dispatch,std::string* diagnostic=nullptr){
    const auto lease=dispatch.origin?dispatch.origin->instruction_lease(frame):std::nullopt;
    if(!lease){if(diagnostic)*diagnostic="resource owner requires an exact actual source instruction lease";return false;}
    return resource_owner_detail::bind_registered(receiver,owner,registered,identities,[&]{
        return lease&&receiver.prepare_instruction_owner(*lease,frame,instruction,dispatch,owner);
    },diagnostic);
}
// A parked45/27 routing callback owns a separate typed service boundary; never
// manufacture an active instruction frame/lease from its value-only context.
inline bool bind_route_shoo_resource_owner(odh::Routing& source,const odh::SourceSparseMotionContext& context,
        odh::Vm& receiver,odh::CodeOwner owner,const std::shared_ptr<const odh::VmProgram>& registered,
        const std::function<bool()>& identities,std::string* diagnostic=nullptr){
    if(!source.source_shoo_owner_binding_current(context)){
        if(diagnostic)*diagnostic="resource owner requires the actual current routing Shoo service";return false;
    }
    return resource_owner_detail::bind_registered(receiver,owner,registered,identities,[&]{
        return source.prepare_source_shoo_owner(context,receiver,owner);
    },diagnostic);
}
}
