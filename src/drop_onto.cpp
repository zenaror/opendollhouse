#include "odh/drop_onto.hpp"
#include <array>
#include <string>
#include <utility>

namespace odh {
namespace {
std::uint16_t read_u16(const std::array<std::uint8_t,8>& operands,std::size_t offset) {
    return static_cast<std::uint16_t>(operands[offset]|(static_cast<std::uint16_t>(operands[offset+1])<<8));
}
std::optional<bool> would_create_cycle(const World& world,std::int16_t container,std::int16_t occupant) {
    const auto* current=world.find(container);
    std::size_t depth=0;
    while(current) {
        if(current->id==occupant)return true;
        if(!current->attachment)return false;
        if(++depth>4096)return {};
        current=world.find(current->attachment->container);
    }
    return {};
}
}

bool DropOnto::bind(const Vm& vm) {
    if(vm_&&vm_!=&vm)return false;
    vm_=&vm;return true;
}

VmPrimitive DropOnto::primitive() {
    return [this](const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,
                  const VmDispatch& dispatch)->PrimitiveOutcome {
        const auto unsupported=[](std::string reason){
            return PrimitiveOutcome{PrimitiveDisposition::unsupported,std::move(reason)};
        };
        if(instruction.opcode!=43)return {};
        if(executing_)return unsupported("DropOnto cannot reenter its slot transaction");
        if(!vm_||dispatch.origin!=vm_||dispatch.check_mode||
           !vm_->matches_active_primitive(frame,instruction,dispatch))
            return unsupported("DropOnto is not bound to this synchronous VM dispatch");
        struct Guard {bool& active;explicit Guard(bool& value):active(value){active=true;}~Guard(){active=false;}} guard(executing_);

        const auto* caller=world_.frame_entity(frame,VmEntityRole::caller,true);
        const auto* destination=world_.frame_entity(frame,VmEntityRole::stack_object,true);
        if(!caller||!destination)
            return unsupported("DropOnto Caller or StackObject identity is not live in World");
        if(caller->ui_ghost||destination->ui_ghost)
            return unsupported("DropOnto ghost container semantics are not represented by native World");

        const auto source_mode=read_u16(instruction.operands,0);
        const auto source_value=read_u16(instruction.operands,2);
        const auto destination_mode=read_u16(instruction.operands,4);
        const auto destination_value=read_u16(instruction.operands,6);
        const auto resolve_slot=[&](std::uint16_t mode,std::uint16_t value)->std::optional<std::size_t> {
            if(mode!=1)return static_cast<std::size_t>(value);
            if(value>=frame.arguments.size())return {};
            return static_cast<std::uint16_t>(frame.arguments[value]);
        };
        const auto source_slot=resolve_slot(source_mode,source_value);
        const auto destination_slot=resolve_slot(destination_mode,destination_value);
        if(!source_slot||!destination_slot)
            return unsupported("DropOnto parameter slot number is outside the current argument frame");

        if(*source_slot>=caller->slots.size()) {
            if(caller->kind==EntityKind::avatar)
                return unsupported("DropOnto avatar source slot outside its direct-indexed slot array");
            return {PrimitiveDisposition::goto_false,{}};
        }
        const auto item_id=caller->slots[*source_slot];
        if(item_id==0)return {PrimitiveDisposition::goto_false,{}};
        if(item_id<0)
            return unsupported("DropOnto source slot contains a nonpositive identity absent from the native profile");
        const auto* item=world_.find(item_id);
        if(!item)
            return unsupported("DropOnto source slot names an absent or retired entity");
        if(item->ui_ghost)
            return unsupported("DropOnto ghost item semantics are not represented by native World");

        if(*destination_slot>=destination->slots.size())
            return unsupported("DropOnto object destination slot is outside the native containment array; pinned PlaceInSlot may detach its source item before returning false, and that failed-operation cleanup side effect is not modeled");
        // Pinned DropOnto tests this before PlaceInSlot, so occupied destinations
        // fail without clearing the source slot or changing carry state.
        if(destination->slots[*destination_slot]!=0)
            return {PrimitiveDisposition::goto_false,{}};
        if(!item->attachment||item->attachment->container!=caller->id||item->attachment->slot!=*source_slot)
            return unsupported("DropOnto Caller slot and item attachment are not reciprocal");
        const auto cycle=would_create_cycle(world_,destination->id,item_id);
        if(!cycle)return unsupported("DropOnto destination ancestry exceeds or violates the World containment profile");
        if(*cycle)
            return {PrimitiveDisposition::goto_false,{}};

        auto move=world_.prepare_move_to_slot(destination->id,*destination_slot,item_id);
        if(!move)
            return unsupported("DropOnto World move preflight cannot represent this containment subtree");
        if(move->already_in_slot())return {PrimitiveDisposition::goto_false,{}};

        std::array<std::int16_t,2> affected_carriers{};std::size_t affected_count=0;
        if(caller->kind==EntityKind::avatar)affected_carriers[affected_count++]=caller->id;
        if(destination->kind==EntityKind::avatar&&*destination_slot==0&&
           (affected_count==0||affected_carriers[0]!=destination->id))
            affected_carriers[affected_count++]=destination->id;

        std::array<std::optional<Playback::PreparedCarryChange>,2> carry_plans;
        for(std::size_t n=0;n<affected_count;++n) {
            if(!carriers_)
                return unsupported("DropOnto avatar slot change has no host Playback resolver");
            std::optional<CarrierController> binding;
            try {binding=carriers_(affected_carriers[n]);}
            catch(const std::exception& error) {
                return unsupported(std::string("DropOnto carrier lookup failed: ")+error.what());
            } catch(...) {return unsupported("DropOnto carrier lookup raised a non-standard exception");}
            if(!binding||!binding->playback||!binding->actor_generation||!binding->vm_generation)
                return unsupported("DropOnto affected avatar has no bound Playback/controller identity");
            std::string why;
            try {
                carry_plans[n]=binding->playback->prepare_slot_carry_change(*move,frame,*vm_,instruction,dispatch,
                    affected_carriers[n],binding->actor_generation,binding->vm_generation,&why);
            } catch(const std::exception& error) {
                return unsupported(std::string("DropOnto carry preflight failed: ")+error.what());
            } catch(...) {return unsupported("DropOnto carry preflight raised a non-standard exception");}
            if(!carry_plans[n])
                return unsupported(why.empty()?"DropOnto avatar carry change is unsupported":std::move(why));
        }

        if(!world_.can_commit_move_to_slot(*move))
            return unsupported("DropOnto World preflight became stale during carrier resolution");
        for(std::size_t n=0;n<affected_count;++n)
            if(!carry_plans[n]||!carry_plans[n]->can_commit())
                return unsupported("DropOnto carry preflight became stale before publication");
        if(transfer_){
            const auto caller_generation=caller->generation;
            SlotTransferResult result;
            try{result=transfer_(frame,instruction,dispatch,*move);}
            catch(const std::exception& error){return unsupported(std::string("DropOnto source slot transfer failed: ")+error.what());}
            catch(...){return unsupported("DropOnto source slot transfer raised a non-standard exception");}
            if(!result.completed)return unsupported(result.diagnostic.empty()?
                "DropOnto source slot lifecycle could not complete":std::move(result.diagnostic));
            const auto* source_actor=world_.frame_entity(frame,VmEntityRole::caller,true);
            if(!source_actor||source_actor->generation!=caller_generation||
               !vm_->matches_active_primitive(frame,instruction,dispatch))
                return unsupported("DropOnto source dispatch or Caller identity changed during slot lifecycle");
            if(!result.placed)return {PrimitiveDisposition::goto_false,{}};
            // Source Pre/Post may legitimately advance World revisions. Check
            // real reciprocal publication rather than reusing its old epoch.
            const auto* moved=world_.find(move->occupant());const auto* container=world_.find(move->container());
            if(!moved||moved->generation!=move->occupant_generation()||!container||
               container->generation!=move->container_generation()||move->slot()>=container->slots.size()||
               container->slots[move->slot()]!=moved->id||!moved->attachment||
               moved->attachment->container!=container->id||moved->attachment->slot!=move->slot())
                return unsupported("DropOnto source slot transfer did not publish its exact reciprocal identities");
        }else if(!world_.commit_move_to_slot(*move))
            return unsupported("DropOnto World publication invariant was violated");

        // Everything capable of resolving resources or allocating ran before
        // World publication. These checks and swaps are bounded/no-callback.
        for(std::size_t n=0;n<affected_count;++n)
            if(!carry_plans[n]->can_publish())
                return unsupported("DropOnto prepared carry identity changed after World publication");
        for(std::size_t n=0;n<affected_count;++n)
            if(!carry_plans[n]->publish())
                return unsupported("DropOnto no-fail carry publication invariant was violated");
        return {PrimitiveDisposition::goto_true,{}};
    };
}

} // namespace odh
