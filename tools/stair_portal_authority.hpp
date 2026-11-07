// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "room_portal_session.hpp"
#include "odh/object_position_valid.hpp"
#include <set>

namespace odh_host {
struct StairPortalAuthorityServices {
    // Reuse the host's validate_portal_context: immutable banks, resource
    // manifest, live edge generation/pose and current architectural graph.
    // This must authenticate the supplied real resident/ticket, not a cache.
    std::function<bool(std::int16_t,std::uint64_t,const RoomPortalContext&)> validate_context;
    // Rebuild from current architecture + exact saved/live stair membership.
    // Return ONLY the lower/upper portal endpoints of the same verified group,
    // with their current generations. Missing/stale geometry stays unknown.
    std::function<std::optional<std::vector<odh::ObjectPositionIdentity>>(odh::ObjectPositionIdentity)> endpoints;
};
struct StairPortalContinuation {
    std::uint64_t ticket=0;
    odh::ObjectPositionIdentity actor,portal,target;
};
// The host supplies its actual resident VM stack and authenticated current
// primitive frame (including Snap's dispatch guard). Nothing here is saved:
// restore reconstructs the ordinary RoomPortalSession and architectural proof.
inline std::optional<StairPortalContinuation> authorize_stair_portal_continuation(
        const odh::World& world,const std::vector<odh::VmFrame>& active_stack,
        const odh::VmFrame& frame,odh::ObjectPositionIdentity target,
        const RoomPortalSession& session,const StairPortalAuthorityServices& services,
        std::string* diagnostic=nullptr) {
    const auto fail=[&](const char* reason)->std::optional<StairPortalContinuation>{if(diagnostic)*diagnostic=reason;return {};};
    if(diagnostic)diagnostic->clear();
    if(!services.validate_context||!services.endpoints||active_stack.empty()||active_stack.size()>64||
       session.contexts.size()>16||!odh::vm_frame_equal(active_stack.back(),frame)||!frame.context.entity_references_bound)
        return fail("stair authority has no authenticated current frame/services");
    const auto* actor=world.frame_entity(frame,odh::VmEntityRole::caller);
    const auto* callee=world.frame_entity(frame,odh::VmEntityRole::callee);
    const auto* destination=world.find(target.id);
    if(!actor||actor->kind!=odh::EntityKind::avatar||actor->dead||actor->ui_ghost||!actor->position||actor->attachment||
       !callee||!destination||destination->generation!=target.generation||destination->kind!=odh::EntityKind::object||
       destination->dead||destination->ui_ghost||!destination->position||destination->attachment)
        return fail("stair actor/callee/target identity unavailable");
    const auto source_frame=frame;const auto actor_position=actor->position;
    // The nearest external frame owns this continuation. A nested unrelated
    // external frame must not borrow an older portal's authority.
    const auto external=std::find_if(active_stack.rbegin(),active_stack.rend(),[](const auto& item){
        return item.return_mode==odh::VmCallReturnMode::external_frame_next_tick;
    });
    if(external==active_stack.rend()||!external->invocation_ticket||!external->context.entity_references_bound)
        return fail("stair continuation has no live external portal frame");
    const auto external_frame=*external;const auto ticket=external_frame.invocation_ticket;
    const auto found=session.contexts.find(ticket);
    if(found==session.contexts.end()||!found->second.pending_call||!found->second.pending_edge||found->second.ignored.size()>32)
        return fail("stair external ticket has no pending portal context");
    const auto context=found->second;const auto& call=*context.pending_call;const auto& edge=*context.pending_edge;
    if(!call.context.entity_references_bound||!call.context.code_owner||call.return_mode!=odh::VmCallReturnMode::ordinary||
       external_frame.key.scope!=call.key.scope||external_frame.key.id!=call.key.id||
       external_frame.context.code_owner!=call.context.code_owner||frame.context.code_owner!=call.context.code_owner||
       external_frame.context.caller!=call.context.caller||frame.context.caller!=call.context.caller||
       external_frame.context.callee!=call.context.callee||frame.context.callee!=call.context.callee||
       call.context.callee!=edge.portal_id||call.context.stack_object!=edge.portal_id)
        return fail("stair pending call differs from actual actor/owner/external frame");
    odh::VmFrame call_address;call_address.context=call.context;
    const auto* call_actor=world.frame_entity(call_address,odh::VmEntityRole::caller);
    const auto* portal=world.frame_entity(call_address,odh::VmEntityRole::callee);
    const auto* external_actor=world.frame_entity(external_frame,odh::VmEntityRole::caller);
    const auto* external_callee=world.frame_entity(external_frame,odh::VmEntityRole::callee);
    if(!call_actor||call_actor!=actor||!external_actor||external_actor!=actor||!portal||portal!=callee||
       !external_callee||external_callee!=portal||portal->kind!=odh::EntityKind::object||portal->dead||portal->ui_ghost||
       portal->attachment||portal->generation!=context.portal_generation||!portal->position||!(*portal->position==edge.position))
        return fail("stair pending portal generation/pose changed");
    const odh::ObjectPositionIdentity actor_id{actor->id,actor->generation},portal_id{portal->id,portal->generation};
    const auto endpoints=services.endpoints(portal_id);
    if(!endpoints||endpoints->size()!=2)return fail("stair endpoint architecture is unknown");
    bool has_portal=false,has_target=false;std::set<std::int16_t> ids,levels;
    std::vector<odh::EntityPosition> positions;
    for(const auto endpoint:*endpoints){const auto* item=world.find(endpoint.id);
        if(!item||item->generation!=endpoint.generation||item->kind!=odh::EntityKind::object||item->dead||item->ui_ghost||
           item->attachment||!item->position||!item->definition||!ids.insert(item->id).second||
           item->position->level<1||item->position->level>2||!levels.insert(item->position->level).second)
            return fail("stair endpoint group identity/levels changed");
        positions.push_back(*item->position);
        has_portal|=item->id==portal_id.id&&item->generation==portal_id.generation;
        has_target|=item->id==target.id&&item->generation==target.generation;
    }
    if(!has_portal||!has_target)return fail("stair target belongs to another endpoint group");
    if(!services.validate_context(actor_id.id,ticket,context))return fail("stair portal resource/architecture validation failed");
    // The resource/architecture resolver is read-only; reject any changed live
    // continuation instead of turning its previous validation into a lease.
    const auto current=session.contexts.find(ticket);
    const auto current_external=std::find_if(active_stack.rbegin(),active_stack.rend(),[](const auto& item){
        return item.return_mode==odh::VmCallReturnMode::external_frame_next_tick;
    });
    const auto* current_actor=world.frame_entity(source_frame,odh::VmEntityRole::caller);
    if(!current_actor||current_actor->kind!=odh::EntityKind::avatar||current_actor->dead||current_actor->ui_ghost||
       !(current_actor->position==actor_position)||current_actor->attachment||
       current_external==active_stack.rend()||!odh::vm_frame_equal(*current_external,external_frame)||active_stack.empty()||!odh::vm_frame_equal(active_stack.back(),source_frame)||current==session.contexts.end()||
       !current->second.pending_call||!current->second.pending_edge||current->second.portal_generation!=context.portal_generation||
       current->second.pending_edge->portal_id!=edge.portal_id||!(current->second.pending_edge->position==edge.position)||
       current->second.pending_edge->source_room!=edge.source_room||current->second.pending_edge->target_room!=edge.target_room||
       current->second.pending_call->key.scope!=call.key.scope||current->second.pending_call->key.id!=call.key.id||
       !odh::vm_context_equal(current->second.pending_call->context,call.context)||
       current->second.pending_call->arguments!=call.arguments||current->second.pending_call->return_mode!=call.return_mode)
        return fail("stair continuation changed during validation");
    for(std::size_t n=0;n<endpoints->size();++n){const auto& endpoint=endpoints->at(n);const auto* item=world.find(endpoint.id);
        if(!item||item->generation!=endpoint.generation||!item->position||!(*item->position==positions[n])||item->attachment||item->dead||item->ui_ghost)
            return fail("stair endpoint changed during validation");}
    return StairPortalContinuation{ticket,actor_id,portal_id,target};
}
inline bool authorize_stair_goto_relative(
        const odh::World& world,const std::vector<odh::VmFrame>& active_stack,
        const odh::VmFrame& frame,const odh::Instruction& instruction,
        const RoomPortalSession& session,const StairPortalAuthorityServices& services,
        std::string* diagnostic=nullptr) {
    if(frame.key.scope!=odh::RoutineScope::private_object||frame.key.id!=4112||instruction.opcode!=27||
       instruction.operands[0]||instruction.operands[1]||instruction.operands[2]!=254||
       (instruction.operands[3]!=0&&instruction.operands[3]!=4)||instruction.operands[4]||
       instruction.operands[5]||instruction.operands[6]||instruction.operands[7]){
        if(diagnostic)*diagnostic="stair GotoRelative profile is unsupported";return false;
    }
    const auto* target=world.frame_entity(frame,odh::VmEntityRole::stack_object);
    return target&&authorize_stair_portal_continuation(world,active_stack,frame,{target->id,target->generation},session,services,diagnostic).has_value();
}
} // namespace odh_host
