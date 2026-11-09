#include "odh/live_entry_point5.hpp"
#include "odh/avatar_position_valid.hpp"
#include "odh/routing.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
AvatarPositionValidResult unknown(std::string reason){
    AvatarPositionValidResult result;result.diagnostic=std::move(reason);return result;
}
AvatarPositionValidResult blocked(std::int16_t error){
    AvatarPositionValidResult result;result.status=AvatarPositionValidStatus::blocked;result.placement_error=error;return result;
}
AvatarPositionValidResult success(){
    AvatarPositionValidResult result;result.status=AvatarPositionValidStatus::success;return result;
}
std::optional<std::uint16_t> word(const Entity& entity,std::int16_t index){
    const auto value=entity.object_words.find(index);
    return value==entity.object_words.end()?std::nullopt:std::optional<std::uint16_t>{static_cast<std::uint16_t>(value->second)};
}
}
struct AvatarPositionValid::PreparedAuthorization::Data {
    const AvatarPositionValid* owner=nullptr;std::weak_ptr<const std::uint8_t> lifetime;const Vm* vm=nullptr;
    VmFrame frame;Instruction instruction;VmDispatch dispatch{};
    std::uint64_t run=0,steps=0,actor_generation=0,caller_generation=0,stack_generation=0,parent_generation=0;
    std::int16_t actor=0;std::optional<Attachment> attachment;
};
std::optional<AvatarPositionValid::PreparedAuthorization> AvatarPositionValid::prepare(
    const VmFrame& frame,const Vm& vm,const Instruction& instruction,const VmDispatch& dispatch,
    std::int16_t id,std::uint64_t generation){
    if(executing_||(instruction.opcode!=5&&instruction.opcode!=16&&instruction.opcode!=46)||
       !vm.matches_active_primitive(frame,instruction,dispatch))return {};
    const auto* actor=world_.find(id);const auto* caller=world_.frame_entity(frame,VmEntityRole::caller,true);
    if(!actor||actor->kind!=EntityKind::avatar||actor->ui_ghost||!generation||actor->generation!=generation||
       !caller||caller->ui_ghost||!std::isfinite(actor->radian_direction))return {};
    bool actual=id==frame.context.caller;
    if(instruction.opcode==16)actual=id==frame.context.stack_object;
    else if(instruction.opcode==5){const auto held=world_.read(frame,20,0);actual=held&&*held==id;}
    if(frame.context.entity_references_bound&&instruction.opcode!=5)
        actual=actual&&(vm_context_reference(frame.context,instruction.opcode==16?VmEntityRole::stack_object:VmEntityRole::caller)==WorldEntityReference{id,generation});
    if(!actual)return {};
    if(!authorization_owner_)authorization_owner_=std::make_shared<const std::uint8_t>(0);
    auto data=std::make_shared<PreparedAuthorization::Data>();data->owner=this;data->lifetime=authorization_owner_;data->vm=&vm;
    data->frame=frame;data->instruction=instruction;data->dispatch=dispatch;
    data->run=vm.run_generation();data->steps=vm.steps();data->actor=id;data->actor_generation=generation;
    data->caller_generation=caller->generation;data->attachment=actor->attachment;
    if(frame.context.stack_object){
        const auto* stack=world_.frame_entity(frame,VmEntityRole::stack_object);
        if(!stack&&(!frame.context.entity_references_bound||frame.context.stack_object_generation))return {};
        data->stack_generation=stack?stack->generation:0;
    }
    if(actor->attachment){const auto* parent=world_.find(actor->attachment->container);
        if(!parent||actor->attachment->slot>=parent->slots.size()||parent->slots[actor->attachment->slot]!=id)return {};
        data->parent_generation=parent->generation;}
    return PreparedAuthorization{std::move(data)};
}
bool AvatarPositionValid::authorization_matches(const PreparedAuthorization& authorization,const VmFrame& frame,
    const Vm& vm,const Instruction& instruction,const VmDispatch& dispatch,std::int16_t id,std::uint64_t generation) const {
    const auto& p=authorization.data_;
    if(!p||p->owner!=this||p->lifetime.lock()!=authorization_owner_||p->vm!=&vm||p->actor!=id||p->actor_generation!=generation||
       p->run!=vm.run_generation()||p->steps!=vm.steps()||p->dispatch.origin!=dispatch.origin||
       p->dispatch.ticket!=dispatch.ticket||p->dispatch.check_mode!=dispatch.check_mode||
       !vm.matches_active_primitive(frame,instruction,dispatch))return false;
    const auto& f=p->frame;const auto& op=p->instruction;
    if(!vm_frame_equal(f,frame)||f.key.scope!=frame.key.scope||f.key.id!=frame.key.id||f.pc!=frame.pc||
       f.context.caller!=frame.context.caller||f.context.callee!=frame.context.callee||
       f.context.stack_object!=frame.context.stack_object||f.context.code_owner!=frame.context.code_owner||
       f.arguments!=frame.arguments||f.locals!=frame.locals||f.return_mode!=frame.return_mode||
       f.invocation_ticket!=frame.invocation_ticket||f.exact_arguments!=frame.exact_arguments||
       op.opcode!=instruction.opcode||op.on_true!=instruction.on_true||op.on_false!=instruction.on_false||op.operands!=instruction.operands)return false;
    const auto* actor=world_.find(id);const auto* caller=world_.frame_entity(frame,VmEntityRole::caller,true);
    if(!actor||actor->generation!=generation||!caller||caller->generation!=p->caller_generation)return false;
    if(frame.context.stack_object){const auto* stack=world_.frame_entity(frame,VmEntityRole::stack_object);
        if(p->stack_generation?(!stack||stack->generation!=p->stack_generation):stack!=nullptr)return false;}
    if(actor->attachment){
        if(!p->attachment||actor->attachment->container!=p->attachment->container||
           actor->attachment->slot!=p->attachment->slot||actor->attachment->positioned!=p->attachment->positioned)return false;
        const auto* parent=world_.find(actor->attachment->container);
        if(!parent||parent->generation!=p->parent_generation||actor->attachment->slot>=parent->slots.size()||
           parent->slots[actor->attachment->slot]!=id)return false;
    }
    return true;
}
AvatarPositionValidResult AvatarPositionValid::evaluate_prepared(const PreparedAuthorization& authorization,
    const VmFrame& frame,const Vm& vm,const Instruction& instruction,const VmDispatch& dispatch,
    std::int16_t id,std::uint64_t generation,const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    if(!authorization_matches(authorization,frame,vm,instruction,dispatch,id,generation))
        return unknown("prepared avatar PositionValid source authorization is stale or belongs to another placement");
    return evaluate_internal(frame,vm,instruction,dispatch,&authorization,nullptr,nullptr,nullptr,id,generation,candidate,direction,flags);
}
AvatarPositionValidResult AvatarPositionValid::evaluate(const VmFrame& frame,const Vm& vm,
    const Instruction& instruction,const VmDispatch& dispatch,std::int16_t id,std::uint64_t generation,
    const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    return evaluate_internal(frame,vm,instruction,dispatch,nullptr,nullptr,nullptr,nullptr,id,generation,candidate,direction,flags);
}
AvatarPositionValidResult AvatarPositionValid::evaluate_creation(const LiveObjectCreationPlacementSource& source,
    std::int16_t id,std::uint64_t generation,const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    try{
        if(!source.source_still_matches||!source.source_still_matches()||!source.source_vm||
            !source.source_run_generation||source.source_vm->run_generation()!=source.source_run_generation||
            source.created_entity!=id||source.created_generation!=generation)
            return unknown("avatar creation placement requires its exact live factory scope");
        if(source.active_dispatch){
            if(!live_object_creation_active_dispatch_matches(source))
                return unknown("avatar creation placement requires its actual active CreateObjectInstance source");
            const auto& active=*source.active_dispatch;
            return evaluate_internal(active.frame,*source.source_vm,active.instruction,active.dispatch,
                nullptr,nullptr,nullptr,&source,id,generation,candidate,direction,flags);
        }
        if(source.source_vm->status()!=VmStatus::waiting||!source.source_vm->pending()||source.source_vm->stack().empty()||
            source.source_vm->pending()->instruction.instruction.opcode!=42)
            return unknown("avatar creation placement requires its actual pending CreateObjectInstance source");
        const auto& vm=*source.source_vm;
        const auto frame=vm.stack().back();const auto instruction=vm.pending()->instruction.instruction;
        return evaluate_internal(frame,vm,instruction,{&vm,vm.pending()->ticket},
            nullptr,nullptr,nullptr,&source,id,generation,candidate,direction,flags);
    }catch(const SourceCheckResetRetirement&){throw;}catch(const std::exception& error){return unknown(error.what());}
}
AvatarPositionValidResult AvatarPositionValid::evaluate_route(const RoutePlacementContext& context,
    std::int16_t id,std::uint64_t generation,const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    if(!context.vm)return unknown("route placement context has no source VM");
    if((context.instruction.opcode!=45&&!is_source_goto_relative_slot_instruction(context.instruction))||
       !context.proof.matches(context.frame,*context.vm,context.instruction,context.dispatch))
        return unknown("route placement requires an authenticated source Route45 or ranged GotoRelative dispatch");
    return evaluate_internal(context.frame,*context.vm,context.instruction,context.dispatch,
        nullptr,&context.proof,nullptr,nullptr,id,generation,candidate,direction,flags);
}
AvatarPositionValidResult AvatarPositionValid::evaluate_execution(const RouteExecutionContext& context,
    std::int16_t id,std::uint64_t generation,const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    if(!context.vm||context.phase!=RouteExecutionPhase::set_position||direction!=0||flags!=0||
       (context.instruction.opcode!=45&&context.instruction.opcode!=27)||
       !context.proof.matches_after_restore(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))
        return unknown("source route SetPosition requires a live typed lease, NORTH and default flags");
    auto result=evaluate_internal(context.frame,*context.vm,context.instruction,context.dispatch,
        nullptr,nullptr,&context,nullptr,id,generation,candidate,direction,flags);
    if(!context.proof.matches_after_restore(context.frame,*context.vm,context.instruction,context.dispatch,context.phase))
        return unknown("source route SetPosition participants changed after restoring temporary heading");
    return result;
}
AvatarPositionValidResult AvatarPositionValid::evaluate_internal(const VmFrame& frame,const Vm& vm,
    const Instruction& instruction,const VmDispatch& dispatch,const PreparedAuthorization* authorization,
    const RoutePlacementProof* route_proof,
    const RouteExecutionContext* execution,
    const LiveObjectCreationPlacementSource* creation,
    std::int16_t id,std::uint64_t generation,const EntityPosition& candidate,
    std::uint8_t direction,std::uint8_t flags){
    const auto source_dispatch_valid=[&](){
        if(creation){
            if(authorization||route_proof||execution||!creation->source_still_matches||!creation->source_still_matches()||
                creation->source_vm!=&vm||creation->source_run_generation!=vm.run_generation()||
                creation->created_entity!=id||creation->created_generation!=generation)return false;
            if(creation->ignored_intersection_group){
                const auto ignored=*creation->ignored_intersection_group;
                if(!world_.is_live_reference(ignored)||ignored.id!=frame.context.caller||
                    (frame.context.entity_references_bound&&vm_context_reference(frame.context,VmEntityRole::caller)!=ignored))return false;
            }
            if(creation->active_dispatch){
                const auto& active=*creation->active_dispatch;
                return live_object_creation_active_dispatch_matches(*creation)&&vm_frame_equal(frame,active.frame)&&
                    instruction.opcode==active.instruction.opcode&&instruction.operands==active.instruction.operands&&
                    instruction.on_true==active.instruction.on_true&&instruction.on_false==active.instruction.on_false&&
                    dispatch.origin==active.dispatch.origin&&dispatch.ticket==active.dispatch.ticket&&dispatch.check_mode==active.dispatch.check_mode;
            }
            if(vm.status()!=VmStatus::waiting||!vm.pending()||vm.stack().empty())return false;
            const auto& pending=*vm.pending();const auto& trace=pending.instruction;
            return dispatch.origin==&vm&&dispatch.ticket==pending.ticket&&!dispatch.check_mode&&
                trace.instruction.opcode==42&&instruction.opcode==42&&trace.instruction.operands==instruction.operands&&
                trace.instruction.on_true==instruction.on_true&&trace.instruction.on_false==instruction.on_false&&
                vm_frame_equal(vm.stack().back(),frame)&&
                vm_context_equal(pending.context,frame.context)&&trace.pc==frame.pc&&
                trace.key.scope==frame.key.scope&&trace.key.id==frame.key.id&&trace.code_owner==frame.context.code_owner;
        }

        if(execution)return !route_proof&&execution->phase==RouteExecutionPhase::set_position&&
            (instruction.opcode==45||instruction.opcode==27)&&
            execution->proof.matches(frame,vm,instruction,dispatch,execution->phase);
        if(instruction.opcode==5||instruction.opcode==16||instruction.opcode==46)
            return vm.matches_active_primitive(frame,instruction,dispatch);
        return (instruction.opcode==45||is_source_goto_relative_slot_instruction(instruction))&&
            route_proof&&route_proof->matches(frame,vm,instruction,dispatch);
    };
    const auto valid=[&](){const auto* actor=world_.find(id);
        bool actual_target=creation?id==creation->created_entity:authorization?authorization_matches(*authorization,frame,vm,instruction,dispatch,id,generation):id==frame.context.caller;
        if(!authorization&&!creation){
            if(instruction.opcode==16)actual_target=id==frame.context.stack_object;
            else if(instruction.opcode==5){const auto held=world_.read(frame,20,0);actual_target=held&&*held==id;}
        }
        if(!creation&&frame.context.entity_references_bound&&instruction.opcode!=5)
            actual_target=actual_target&&(vm_context_reference(frame.context,instruction.opcode==16?VmEntityRole::stack_object:VmEntityRole::caller)==WorldEntityReference{id,generation});
        return actual_target&&source_dispatch_valid()&&
            actor&&actor->kind==EntityKind::avatar&&generation&&actor->generation==generation&&
            !actor->ui_ghost&&std::isfinite(actor->radian_direction);};
    const auto prepare_request=[&](AvatarPositionEntryPoint5Request& request){
        if(creation)request.object_creation_source=*creation;
        else {request.source_vm=&vm;request.source_frame=&frame;request.source_instruction=&instruction;request.source_dispatch=dispatch;}
        if(route_proof)request.route_placement_proof=*route_proof;
        if(execution){request.route_execution_proof=execution->proof;request.route_execution_phase=execution->phase;}
    };
    return evaluate_geometry(valid,prepare_request,creation?creation->ignored_intersection_group:std::nullopt,
        id,generation,candidate,direction,flags);
}
AvatarPositionValidResult AvatarPositionValid::evaluate_lifecycle(const LiveRetiredContainerPlacementSource& source,
    const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    const auto valid=[&]{
        const auto* actor=world_.find(source.child.id);
        return source.world==&world_&&world_.retired_child_authorized(source.container,source.child)&&
            source.source_still_matches&&source.source_still_matches()&&actor&&actor->kind==EntityKind::avatar&&
            !actor->ui_ghost&&std::isfinite(actor->radian_direction);
    };
    return evaluate_geometry(valid,[&](AvatarPositionEntryPoint5Request& request){request.retired_container_source=source;},
        {},source.child.id,source.child.generation,candidate,direction,flags);
}
AvatarPositionValidResult AvatarPositionValid::evaluate_constructor(const LiveAvatarConstructorPlacementSource& source,
    const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    const auto valid=[&]{
        if(!live_avatar_constructor_placement_source_matches(source,world_))return false;
        const auto* person=world_.resolve_reference(source.avatar);
        return person&&std::isfinite(person->radian_direction);
    };
    return evaluate_geometry(valid,[&](AvatarPositionEntryPoint5Request& request){request.avatar_constructor_source=source;},
        {},source.avatar.id,source.avatar.generation,candidate,direction,flags);
}
AvatarPositionValidResult AvatarPositionValid::evaluate_recovery(const LiveOriginalRecoveryPlacementSource& source,
    const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    const auto valid=[&]{
        if(!live_original_recovery_placement_source_matches(source,world_))return false;
        const auto* person=world_.resolve_reference(source.person);
        return person&&std::isfinite(person->radian_direction);
    };
    return evaluate_geometry(valid,[&](AvatarPositionEntryPoint5Request& request){request.original_recovery_source=source;},
        {},source.person.id,source.person.generation,candidate,direction,flags);
}
AvatarPositionValidResult AvatarPositionValid::evaluate_geometry(const std::function<bool()>& valid,
    const std::function<void(AvatarPositionEntryPoint5Request&)>& prepare_request,
    std::optional<WorldEntityReference> ignored_group,std::int16_t id,std::uint64_t generation,
    const EntityPosition& candidate,std::uint8_t direction,std::uint8_t flags){
    if(executing_)return unknown("avatar PositionValid cannot reenter");
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(executing_);
    if(!valid()||direction>7||(flags&~15U)||candidate.x<INT16_MIN||candidate.x>INT16_MAX||
       candidate.y<INT16_MIN||candidate.y>INT16_MAX||candidate.level<INT8_MIN||candidate.level>INT8_MAX)
        return unknown("avatar PositionValid source identity, direction or LotTilePos is unavailable");
    if(candidate.x==INT16_MIN&&candidate.y==INT16_MIN&&candidate.level==1)return success();
    try {
        if(!services_.architecture)return unknown("avatar placement architecture is unavailable");
        const auto architecture=services_.architecture(id,candidate);
        if(!architecture||!valid())return unknown("avatar placement architecture or actor identity changed");
        if(candidate.x<0||candidate.y<0||candidate.level<1||architecture->out_of_bounds||
           ((flags&2U)&&architecture->user_out_of_bounds))return blocked(0);
        const auto placement=word(*world_.find(id),42);
        if(!placement)return unknown("avatar PlacementFlags are unavailable");
        const auto floor=architecture->precise_floor;
        if(floor==65535){if((*placement&((1U<<11)|(1U<<12)))==0)return blocked(28);}
        else {
            if(*placement&(1U<<12))return blocked(30);
            if(floor==65534){if((*placement&((1U<<2)|(1U<<13)))==0)return blocked(28);}
            else {
                if(*placement&(1U<<13))return blocked(29);
                if(floor==0){
                    if(candidate.level==1){if((*placement&(1U<<1))==0)return blocked(19);}
                    else if((*placement&(1U<<9))==0){
                        if(!architecture->level_offset)return unknown("avatar upstairs floor requires exact OBJD LevelOffset");
                        if(*architecture->level_offset==0)return blocked(17);
                    }
                }
            }
        }
        // SlopeValid is unconditionally successful for VMAvatar. AllowIntersection
        // bypasses only overlap checks, after architecture/floor acceptance.
        if(flags&4U)return success();
        const auto actor_flags=word(*world_.find(id),8);
        if(!actor_flags)return unknown("avatar intersection flags are unavailable");
        const bool all_solid=(flags&8U)!=0;
        const bool me_allow_avatars=(*actor_flags&16U)&&!all_solid;
        const float old_direction=world_.find(id)->radian_direction;
        struct RestoreDirection {
            World& world;std::int16_t actor;std::uint64_t generation;float radians;
            ~RestoreDirection(){const auto* current=world.find(actor);
                if(current&&current->kind==EntityKind::avatar&&current->generation==generation)
                    (void)world.set_radian_direction(actor,radians);}
        } restore{world_,id,generation,old_direction};
        constexpr double pi=3.141592653589793238462643383279502884;
        if(!world_.set_radian_direction(id,static_cast<float>(direction)*static_cast<float>(pi/4.0)))
            return unknown("temporary avatar placement direction is unavailable");
        const auto* oriented_actor=world_.find(id);
        if(!oriented_actor->kill_timeout)return unknown("avatar temporary footprint lifecycle is unavailable");
        if(*oriented_actor->kill_timeout>-1&&(*actor_flags&4U)==0)return success();
        if(!services_.intersections)return unknown("avatar placement RoomInfo query is unavailable");
        auto query=services_.intersections(id,candidate,direction);
        if(query.status!=RoomInfoObstacleIndexStatus::complete||query.intersections.size()>4096||!valid())
            return unknown("avatar placement RoomInfo is incomplete or actor identity changed");
        std::stable_sort(query.intersections.begin(),query.intersections.end(),[](const auto& a,const auto& b){return a.owner_id<b.owner_id;});
        AvatarPositionValidResult result=success();
        for(const auto& obstacle:query.intersections){
            if(obstacle.owner_id<=0||obstacle.owner_id>INT16_MAX)return unknown("RoomInfo obstacle identity is invalid");
            const auto other=static_cast<std::int16_t>(obstacle.owner_id);
            if(other==id)continue;
            const auto* entity=world_.find(other);
            if(!entity||!services_.same_group)return unknown("placement obstacle or authoritative group is unavailable");
            const auto other_generation=entity->generation;
            if(ignored_group){
                const auto ignored=services_.same_group(ignored_group->id,other);
                if(!ignored||!valid())return unknown("creation retry Caller group identity is unavailable");
                entity=world_.find(other);
                if(!entity||entity->generation!=other_generation)return unknown("creation retry obstacle identity changed");
                if(*ignored)continue;
            }
            const auto group=services_.same_group(id,other);
            if(!group||!valid())return unknown("placement group identity is unavailable");
            entity=world_.find(other);
            if(!entity||entity->generation!=other_generation)return unknown("placement obstacle generation changed");
            if(*group)continue;
            const auto ghost=static_cast<std::int16_t>(world_.find(id)->ui_ghost||entity->ui_ghost?1:0);
            const auto execute=[&](std::int16_t tested,std::uint64_t tested_generation,std::int16_t counterpart,
                std::uint64_t counterpart_generation)->std::optional<bool>{
                const auto* current=world_.find(tested);
                if(!current||current->generation!=tested_generation||!current->entry_points)return {};
                if(current->entry_points->size()<=5||(*current->entry_points)[5].action<=255)return false;
                if(!services_.entry_point5)return {};
                AvatarPositionEntryPoint5Request request;
                request.tested_entity=tested;request.tested_generation=tested_generation;
                request.other=counterpart;request.other_generation=counterpart_generation;
                request.action=(*current->entry_points)[5].action;request.ghost=ghost;request.arguments={counterpart,ghost,0,0};
                prepare_request(request);
                auto answer=services_.entry_point5(request);
                const auto* after=world_.find(tested);const auto* paired=world_.find(counterpart);
                if(!valid()||!after||after->generation!=tested_generation||!paired||paired->generation!=counterpart_generation)return {};
                return answer;
            };
            const auto actor_exempt=execute(id,generation,other,other_generation);
            if(!actor_exempt)return unknown("actor EP5 live execution is unavailable");
            if(*actor_exempt)continue;
            const auto other_exempt=execute(other,other_generation,id,generation);
            if(!other_exempt)return unknown("obstacle EP5 live execution is unavailable");
            if(*other_exempt)continue;
            entity=world_.find(other);
            if(!entity||entity->generation!=other_generation)return unknown("placement blocker was replaced");
            const auto blocker_flags=word(*entity,8);
            if(!blocker_flags)return unknown("placement blocker flags are unavailable");
            const bool avatar=entity->kind==EntityKind::avatar;
            bool allow=(avatar&&me_allow_avatars)||((*blocker_flags&2U)==0&&(*blocker_flags&16U)!=0);
            if(all_solid&&avatar)allow=false;
            if(allow)continue;
            result=blocked(10);result.blocker=other;result.blocker_generation=other_generation;result.blocker_is_avatar=avatar;
            if(!entity->entry_points||entity->entry_points->size()<=26)
                return unknown("placement blocker EP26 metadata is unavailable");
            if((*entity->entry_points)[26].action!=0)break;
        }
        return result;
    }catch(const SourceCheckResetRetirement&){throw;}catch(const std::exception& error){return unknown(std::string("avatar PositionValid callback failed: ")+error.what());}
    catch(...){return unknown("avatar PositionValid callback raised a non-standard exception");}
}
}
