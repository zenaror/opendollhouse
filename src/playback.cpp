// Source locomotion collection sections port FreeSO contributors' VMAvatar.Tick
// and VMRoutingFrame.StartWalkAnimation/PreExit at7e76c1960252ddd033d8937d5edb47d326f5b69c.
// Those source portions are covered by MPL-2.0: https://www.mozilla.org/MPL/2.0/ .
#include "odh/playback.hpp"
#include "odh/avatar_containment.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace odh {
namespace {
std::optional<HandGesture> gesture(const std::string& text){
    const auto space=[](char c){return c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v';};
    const auto* first=text.data();const auto* last=first+text.size();
    while(first!=last&&space(*first))++first;
    while(first!=last&&space(last[-1]))--last;
    if(first!=last&&*first=='+'){++first;if(first!=last&&*first=='-')return {};}
    int value=0;const auto r=std::from_chars(first,last,value);
    if(r.ec!=std::errc{}||r.ptr!=last||value<0||value>3)return {};
    return static_cast<HandGesture>(value);
}
const std::string* property(const AnimationProperties& pairs,const char* key){
    for(const auto& pair:pairs)if(pair.first==key)return &pair.second;
    return nullptr;
}
PrimitiveOutcome unsupported(const std::string& text) {return {PrimitiveDisposition::unsupported,text};}
std::int16_t event_code(const std::string& text) {
    auto first=text.data(),last=first+text.size();
    const auto space=[](char c){return c==' '||c=='\t'||c=='\n'||c=='\r';};
    while(first!=last&&space(*first))++first;
    while(first!=last&&space(last[-1]))--last;
    if(first!=last&&*first=='+'){++first;if(first!=last&&*first=='-')return 0;}
    int value=0;const auto result=std::from_chars(first,last,value);
    return result.ec==std::errc{}&&result.ptr==last&&value>=-32768&&value<=32767?static_cast<std::int16_t>(value):0;
}
PlaybackResourceKey resource_key(const VmFrame& frame,const Entity& actor,PlaybackResourceKey::Kind kind,
                                 std::uint32_t source,std::uint16_t id,std::int16_t posture=0) {
    PlaybackResourceKey key{kind,source,id,posture};key.caller=frame.context.caller;key.callee=frame.context.callee;
    key.stack_object=frame.context.stack_object;key.code_owner=frame.context.code_owner;
    key.actor_age=actor.person_data.size()>58?actor.person_data[58]:0;return key;
}
bool wait_matches_vm(const PlaybackWaitSnapshot& saved,const PlaybackResourceKey& resource,
                     std::int16_t actor,const VmSnapshot& vm) {
    if(vm.status!=VmStatus::waiting||!vm.pending||vm.pending->ticket!=saved.ticket||
       vm.pending->context.caller!=actor||!vm.pending->instruction.depth||
       vm.pending->instruction.depth>vm.stack.size())return false;
    const auto& trace=vm.pending->instruction;const auto& instruction=trace.instruction;
    if(instruction.opcode!=44||resource.kind!=PlaybackResourceKey::Kind::animation||
       resource.caller!=vm.pending->context.caller||resource.callee!=vm.pending->context.callee||
       resource.stack_object!=vm.pending->context.stack_object||resource.code_owner!=vm.pending->context.code_owner)return false;
    const auto flags=instruction.operands[5];
    const auto mode=(flags&1U)|((flags>>3)&2U);
    if(mode!=0&&mode!=3)return false;
    const auto bank=(flags&32U)?VmFrameBank::locals:VmFrameBank::arguments;
    const auto index=static_cast<std::size_t>((flags&32U)?instruction.operands[2]:0);
    const auto& frame=vm.stack[trace.depth-1];
    if(saved.bank!=bank||saved.index!=index||saved.expected!=instruction.operands[6]||
       (saved.bank==VmFrameBank::arguments?saved.index>=frame.arguments.size():
        saved.index>=frame.locals.size()&&saved.index>=256))return false;
    auto id=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
    std::uint32_t source=instruction.operands[4];
    if(flags&4U){if(id>=frame.arguments.size())return false;id=static_cast<std::uint16_t>(frame.arguments[id]);if(source==0)source=65536;}
    return id!=0&&resource.source==source&&resource.id==id&&resource.posture==0;
}
bool same_context(const VmContext& a,const VmContext& b) {
    return vm_context_equal(a,b);
}
bool same_pending(const VmPending& a,const VmPending& b) {
    return a.ticket==b.ticket&&same_context(a.context,b.context)&&a.instruction.step==b.instruction.step&&
        a.instruction.depth==b.instruction.depth&&a.instruction.key.scope==b.instruction.key.scope&&
        a.instruction.key.id==b.instruction.key.id&&a.instruction.pc==b.instruction.pc&&
        a.instruction.code_owner==b.instruction.code_owner&&
        a.instruction.instruction.opcode==b.instruction.instruction.opcode&&
        a.instruction.instruction.operands==b.instruction.instruction.operands&&
        a.instruction.instruction.on_true==b.instruction.instruction.on_true&&
        a.instruction.instruction.on_false==b.instruction.instruction.on_false;
}
bool same_frame(const VmFrame& a,const VmFrame& b) {
    return a.key.scope==b.key.scope&&a.key.id==b.key.id&&a.pc==b.pc&&same_context(a.context,b.context)&&
        a.arguments==b.arguments&&a.locals==b.locals&&a.return_mode==b.return_mode&&
        a.invocation_ticket==b.invocation_ticket&&a.exact_arguments==b.exact_arguments&&a.action_tree==b.action_tree;
}
bool same_attachment(const std::optional<Attachment>& a,const std::optional<Attachment>& b){
    return bool(a)==bool(b)&&(!a||(a->container==b->container&&a->slot==b->slot&&a->positioned==b->positioned));
}
std::array<float,3> walk_weights(std::int32_t velocity){
    return {static_cast<float>(8-velocity)/8.0f,(static_cast<float>(velocity)/8.0f)*0.66f,
        (static_cast<float>(velocity)/8.0f)*0.33f};
}
bool equal_weights(const std::array<float,3>& a,const std::array<float,3>& b){
    for(std::size_t n=0;n<3;++n){std::uint32_t x=0,y=0;std::memcpy(&x,&a[n],sizeof(x));std::memcpy(&y,&b[n],sizeof(y));if(x!=y)return false;}
    return true;
}
bool valid_walk_weights(const std::array<float,3>& weights){
    if(equal_weights(weights,std::array<float,3>{0,0.66f,0.33f}))return true;
    for(std::int32_t v=0;v<=8;++v)if(equal_weights(weights,walk_weights(v)))return true;
    return false;
}
bool locomotion_key(const PlaybackResourceKey& key){
    return key.kind==PlaybackResourceKey::Kind::source_locomotion&&key.source<=1&&key.posture==0&&
        (key.id==3||key.id==20||key.id==21||key.id==25);
}
bool same_resource_context(const PlaybackResourceKey& a,const PlaybackResourceKey& b){
    return a.source==b.source&&a.caller==b.caller&&a.callee==b.callee&&a.stack_object==b.stack_object&&
        a.actor_age==b.actor_age&&a.code_owner==b.code_owner;
}
bool animation_pending_shape(const VmPending& pending,const VmFrame& frame) {
    const auto& trace=pending.instruction;const auto& instruction=trace.instruction;
    if(!pending.ticket||instruction.opcode!=44||!same_context(pending.context,frame.context)||
       trace.key.scope!=frame.key.scope||trace.key.id!=frame.key.id||trace.pc!=frame.pc||
       trace.code_owner!=frame.context.code_owner)return false;
    const auto flags=instruction.operands[5];const auto mode=(flags&1U)|((flags>>3)&2U);
    if((flags&128U)||(mode!=0&&mode!=3))return false;
    auto id=static_cast<std::uint16_t>(instruction.operands[0]|(instruction.operands[1]<<8));
    if(flags&4U){if(id>=frame.arguments.size())return false;id=static_cast<std::uint16_t>(frame.arguments[id]);}
    if(!id)return false; // Reset/carry never owns a native wait.
    return (flags&32U)||!frame.arguments.empty(); // LocalNumber is an unsigned byte, with bounded VM growth.
}

}
bool source_walk_weights_valid(const std::array<float,3>& weights) noexcept {return valid_walk_weights(weights);}
bool Playback::bind(Vm& vm) {if(executing_||(vm_&&vm_!=&vm))return false;vm_=&vm;return true;}
bool Playback::validates_immediate_parent(const VmPending& pending,const VmFrame& frame) const {
    if(executing_||!vm_||vm_->dispatch_active()||!vm_->run_generation()||vm_->stack().empty()||
       !animation_pending_shape(pending,frame)||!pending.instruction.depth||
       pending.instruction.depth>vm_->stack().size())return false;
    const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
    if(!actor||actor->kind!=EntityKind::avatar||actor->ui_ghost||!actor->generation||
       frame.context.caller!=vm_->stack().front().context.caller||
       !same_frame(frame,vm_->stack()[pending.instruction.depth-1])||
       !vm_->has_routine(frame.key,frame.context.code_owner))return false;
    if((state_&&(state_->actor!=actor->id||state_->actor_generation!=actor->generation||
                 state_->vm_generation!=vm_->run_generation()))||
       (hands_&&(hands_->actor!=actor->id||hands_->actor_generation!=actor->generation||
                 hands_->vm_generation!=vm_->run_generation())))return false;
    if(vm_->pending()&&same_pending(pending,*vm_->pending()))
        return vm_->status()==VmStatus::waiting&&pending.instruction.depth==vm_->stack().size();
    for(const auto& continuation:vm_->immediate_continuations())
        if(continuation.parent_pending&&same_pending(pending,*continuation.parent_pending))
            return continuation.parent_status==VmStatus::waiting&&
                continuation.parent_depth==pending.instruction.depth;
    return false;
}
bool Playback::can_park_for_immediate(const VmPending& pending,const VmFrame& frame) const {
    if(!validates_immediate_parent(pending,frame)||!state_||!wait_||
       !vm_->pending()||!same_pending(pending,*vm_->pending())||wait_->ticket!=pending.ticket)return false;
    try{return wait_matches_vm({wait_->ticket,wait_->bank,wait_->index,wait_->expected},
        state_->resource,state_->actor,vm_->snapshot());}catch(...){return false;}
}
bool Playback::park_for_immediate(std::uint64_t ticket) {
    if(executing_||!vm_||!state_||!wait_||!ticket||wait_->ticket!=ticket||
       vm_->immediate_continuations().empty()||vm_->immediate_return())return false;
    const auto& continuation=vm_->immediate_continuations().back();
    if(!continuation.parent_pending||continuation.parent_pending->ticket!=ticket||!continuation.parent_depth||
       continuation.parent_depth>=vm_->stack().size())return false;
    const auto& pending=*continuation.parent_pending;
    if(!validates_immediate_parent(pending,vm_->stack()[continuation.parent_depth-1]))return false;
    // Only a value view is trimmed; no live frame/register or animation changes.
    auto parent=vm_->snapshot();parent.status=continuation.parent_status;parent.pending=pending;
    parent.stack.resize(continuation.parent_depth);
    if(!wait_matches_vm({wait_->ticket,wait_->bank,wait_->index,wait_->expected},
        state_->resource,state_->actor,parent))return false;
    wait_.reset();return true;
}
bool Playback::retry_immediate_parent(std::uint64_t ticket) {
    if(executing_||!vm_||wait_||!ticket||!vm_->pending()||vm_->pending()->ticket!=ticket||vm_->stack().empty()||
       !validates_immediate_parent(*vm_->pending(),vm_->stack().back()))return false;
    return vm_->retry_pending(ticket);
}
bool Playback::service_wait() {
    if(executing_)throw std::logic_error("animation service must not reenter playback");
    if(!vm_||vm_->dispatch_active())return false;
    if(!wait_)return !vm_->pending()||vm_->pending()->instruction.instruction.opcode!=44;
    if(!vm_->pending()||vm_->stack().empty()||!can_park_for_immediate(*vm_->pending(),vm_->stack().back()))return false;
    if(!vm_->retry_pending(wait_->ticket))return false;
    wait_.reset();return true;
}
bool Playback::valid_slot_context(const VmFrame& frame,const Vm& vm,std::uint64_t actor_generation,
    std::uint64_t vm_generation,std::string* error,const Instruction* lifecycle_instruction,const VmDispatch* lifecycle_dispatch) const {
    const auto reject=[&](const char* why){if(error)*error=why;return false;};
    const bool primitive=lifecycle_instruction&&lifecycle_dispatch&&
        vm.matches_active_primitive(frame,*lifecycle_instruction,*lifecycle_dispatch);
    if((lifecycle_instruction||lifecycle_dispatch)&&!primitive)
        return reject("route PreExit requires its exact active primitive authority");
    if(vm_!=&vm||(vm.dispatch_active()&&!primitive)||!vm_generation||vm.run_generation()!=vm_generation||vm.stack().empty())
        return reject("avatar slot carry requires the captured bound VM at a safe point");
    if(vm.status()!=VmStatus::running&&vm.status()!=VmStatus::budget_exhausted&&
       vm.status()!=VmStatus::waiting&&vm.status()!=VmStatus::yielded)
        return reject("avatar slot carry source VM is not an active continuation");
    const auto& active=vm.stack().back();
    if(active.key.scope!=frame.key.scope||active.key.id!=frame.key.id||active.pc!=frame.pc||
       !vm_context_equal(active.context,frame.context))
        return reject("avatar slot carry source frame no longer matches the active VM context");
    const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
    if(!actor||actor->kind!=EntityKind::avatar||actor->ui_ghost||!actor_generation||
       actor->generation!=actor_generation||actor->person_data.size()<=58)
        return reject("avatar slot carry actor identity is absent or stale");
    if((state_&&(state_->actor!=actor->id||state_->actor_generation!=actor_generation||state_->vm_generation!=vm_generation||
                state_->resource.actor_age!=actor->person_data[58]))||
       (hands_&&(hands_->actor!=actor->id||hands_->actor_generation!=actor_generation||hands_->vm_generation!=vm_generation))||
       (carry_resource_&&carry_resource_->caller!=actor->id))
        return reject("avatar slot carry controller belongs to another actor or VM run");
    return true;
}
bool Playback::attach_default_carry(const VmFrame& frame,const Vm& vm,std::uint64_t actor_generation,
    std::uint64_t vm_generation,std::int16_t object,std::uint64_t object_generation,std::string* error) {
    const auto reject=[&](const char* why){if(error)*error=why;return false;};
    if(executing_)return reject("avatar slot carry hook cannot reenter playback");
    if(!valid_slot_context(frame,vm,actor_generation,vm_generation,error))return false;
    const auto attached=[&]{
        const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);const auto* carried=world_.find(object);
        return actor&&actor->generation==actor_generation&&!actor->slots.empty()&&actor->slots[0]==object&&
            object>0&&object_generation&&carried&&carried->generation==object_generation&&!carried->ui_ghost&&
            carried->attachment&&carried->attachment->container==actor->id&&carried->attachment->slot==0;
    };
    if(!attached())return reject("default carry requires the exact reciprocal new slot0 attachment");
    if(!reset_resolver_)return reject("default carry animation resource resolver is unavailable");
    const auto key=resource_key(frame,*world_.frame_entity(frame,VmEntityRole::caller),PlaybackResourceKey::Kind::posture_reset,0,0,-1);
    std::shared_ptr<const AnimationHeader> clip;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
    try {clip=reset_resolver_(frame,-1);}
    catch(const std::exception& exception){if(error)*error=exception.what();return false;}
    catch(...){return reject("default carry animation resolver raised a non-standard exception");}
    if(!clip)return reject("default carry animation resource did not resolve");
    if(!valid_slot_context(frame,vm,actor_generation,vm_generation,error))return false;
    if(!attached()||world_.frame_entity(frame,VmEntityRole::caller)->person_data[58]!=key.actor_age)
        return reject("avatar slot carry identity or attachment changed during resource resolution");
    carry_clip_=std::move(clip);carry_resource_=key;
    if(error)error->clear();return true;
}
bool Playback::clear_slot_carry(const VmFrame& frame,const Vm& vm,std::uint64_t actor_generation,
    std::uint64_t vm_generation,std::size_t slot,std::string* error) {
    const auto reject=[&](const char* why){if(error)*error=why;return false;};
    if(executing_)return reject("avatar slot carry hook cannot reenter playback");
    if(!valid_slot_context(frame,vm,actor_generation,vm_generation,error))return false;
    const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
    if(slot>=actor->slots.size()||actor->slots[slot]!=0)
        return reject("carry clearing requires an actually cleared avatar slot");
    carry_clip_.reset();carry_resource_.reset();if(error)error->clear();return true;
}
struct Playback::PreparedCarryChange::Data {
    Playback* controller=nullptr;
    PreparedSlotMove move;
    explicit Data(const PreparedSlotMove& prepared):move(prepared){}
    VmFrame source_frame,carrier_frame;
    const Vm* source_vm=nullptr;const Vm* carrier_vm=nullptr;
    Instruction instruction{};VmDispatch dispatch{};
    std::uint64_t source_run=0,carrier_run=0,source_steps=0,carrier_steps=0,carrier_generation=0,source_actor_generation=0;
    std::int16_t carrier=0,age=0;
    VmStatus carrier_status=VmStatus::ready;
    std::optional<VmPending> carrier_pending;
    std::shared_ptr<const AnimationHeader> previous_clip,next_clip;
    std::optional<PlaybackResourceKey> previous_resource,next_resource;
    bool clear=false,install=false,consumed=false;
    bool identity() const noexcept {
        try {
            if(consumed||!controller||!source_vm||!carrier_vm||controller->executing_||
               controller->vm_!=carrier_vm||dispatch.origin!=source_vm||dispatch.check_mode||!dispatch.ticket||
               !source_vm->dispatch_active()||source_vm->run_generation()!=source_run||
               source_vm->steps()!=source_steps||source_vm->stack().empty()||
               !same_frame(source_frame,source_vm->stack().back())||(instruction.opcode!=4&&instruction.opcode!=43&&instruction.opcode!=46&&instruction.opcode!=47&&instruction.opcode!=5)||
               carrier_vm->run_generation()!=carrier_run||carrier_vm->steps()!=carrier_steps||
               carrier_vm->status()!=carrier_status||carrier_vm->stack().empty()||
               !same_frame(carrier_frame,carrier_vm->stack().back())||
               (carrier_vm->dispatch_active()&&carrier_vm!=source_vm)||
               bool(carrier_pending)!=bool(carrier_vm->pending())||
               (carrier_pending&&!same_pending(*carrier_pending,*carrier_vm->pending())))return false;
            if(!source_vm->matches_active_primitive(source_frame,instruction,dispatch))return false;
            const auto* source_actor=controller->world_.frame_entity(source_frame,VmEntityRole::caller);
            if(!source_actor||source_actor->ui_ghost||source_actor->generation!=source_actor_generation)return false;
            const auto* actor=controller->world_.find(carrier);
            if(!actor||actor->kind!=EntityKind::avatar||actor->ui_ghost||actor->generation!=carrier_generation||
               actor->person_data.size()<=58||actor->person_data[58]!=age||
               carrier_frame.context.caller!=carrier||carrier_vm->stack().front().context.caller!=carrier||
               controller->carry_clip_!=previous_clip||!(controller->carry_resource_==previous_resource))return false;
            const auto& state=controller->state_;const auto& hands=controller->hands_;
            if((state&&(state->actor!=carrier||state->actor_generation!=carrier_generation||state->vm_generation!=carrier_run))||
               (hands&&(hands->actor!=carrier||hands->actor_generation!=carrier_generation||hands->vm_generation!=carrier_run))||
               (controller->wait_&&(!carrier_vm->pending()||carrier_vm->pending()->ticket!=controller->wait_->ticket)))return false;
            return true;
        }catch(...){return false;}
    }
};
Playback::PreparedCarryChange::PreparedCarryChange(std::unique_ptr<Data> data) noexcept:data_(std::move(data)){}
Playback::PreparedCarryChange::PreparedCarryChange(PreparedCarryChange&&) noexcept=default;
Playback::PreparedCarryChange& Playback::PreparedCarryChange::operator=(PreparedCarryChange&&) noexcept=default;
Playback::PreparedCarryChange::~PreparedCarryChange()=default;
bool Playback::PreparedCarryChange::can_commit() const noexcept {
    return data_&&data_->identity()&&data_->controller->world_.can_commit_move_to_slot(data_->move);
}
bool Playback::PreparedCarryChange::can_publish() const noexcept {
    if(!data_||!data_->identity())return false;
    const auto& d=*data_;const auto& world=d.controller->world_;
    const auto* object=world.find(d.move.occupant());const auto* destination=world.find(d.move.container());
    if(!object||object->generation!=d.move.occupant_generation()||!destination||
       destination->generation!=d.move.container_generation()||d.move.slot()>=destination->slots.size()||
       destination->slots[d.move.slot()]!=object->id||!object->attachment||
       object->attachment->container!=destination->id||object->attachment->slot!=d.move.slot())return false;
    if(d.clear){const auto& attachment=*d.move.source_attachment();
        const auto* source=world.find(attachment.container);
        if(!source||source->generation!=d.move.source_generation()||attachment.slot>=source->slots.size()||
           source->slots[attachment.slot]!=0)return false;
    }
    return true;
}
bool Playback::PreparedCarryChange::publish() noexcept {
    if(!can_publish())return false;
    auto& d=*data_;
    d.controller->carry_clip_.swap(d.next_clip);
    d.controller->carry_resource_.swap(d.next_resource);
    d.consumed=true;return true;
}
std::optional<Playback::PreparedCarryChange> Playback::prepare_slot_carry_change(const PreparedSlotMove& move,
    const VmFrame& frame,const Vm& source_vm,const Instruction& instruction,const VmDispatch& dispatch,
    std::int16_t carrier,std::uint64_t generation,std::uint64_t run,std::string* error) {
    const auto reject=[&](const char* why)->std::optional<PreparedCarryChange>{if(error)*error=why;return {};};
    if(executing_||!vm_||!generation||!run||move.already_in_slot()||!world_.can_commit_move_to_slot(move))
        return reject("carry transfer requires a live non-noop prepared World move");
    const auto* source_actor=world_.frame_entity(frame,VmEntityRole::caller);
    if(!source_actor||source_actor->ui_ghost)return reject("carry transfer source actor is absent or ghosted");
    const auto* actor=world_.find(carrier);
    if(!actor||actor->kind!=EntityKind::avatar||actor->ui_ghost||actor->generation!=generation||
       actor->person_data.size()<=58||vm_->run_generation()!=run||vm_->stack().empty()||
       vm_->stack().back().context.caller!=carrier||vm_->stack().front().context.caller!=carrier)
        return reject("carry transfer requires the carrier's own live bound VM and actor identity");
    const bool clear=move.source_attachment()&&move.source_attachment()->container==carrier;
    const bool install=move.container()==carrier&&move.slot()==0;
    if(!clear&&!install)return reject("prepared World move has no carry effect on this avatar");
    auto data=std::make_unique<PreparedCarryChange::Data>(move);
    data->controller=this;data->source_frame=frame;data->carrier_frame=vm_->stack().back();
    data->source_vm=&source_vm;data->carrier_vm=vm_;data->instruction=instruction;data->dispatch=dispatch;
    data->source_run=source_vm.run_generation();data->source_steps=source_vm.steps();
    data->source_actor_generation=source_actor->generation;
    data->carrier_run=run;data->carrier_steps=vm_->steps();data->carrier_status=vm_->status();data->carrier_pending=vm_->pending();
    data->carrier=carrier;data->carrier_generation=generation;data->age=actor->person_data[58];
    data->previous_clip=carry_clip_;data->previous_resource=carry_resource_;data->clear=clear;data->install=install;
    PreparedCarryChange prepared(std::move(data));
    if(!prepared.can_commit())return reject("carry transfer source dispatch or carrier execution graph is stale");
    if(install){
        if(!reset_resolver_)return reject("destination avatar default carry resolver is unavailable");
        auto& d=*prepared.data_;
        d.next_resource=resource_key(d.carrier_frame,*actor,PlaybackResourceKey::Kind::posture_reset,0,0,-1);
        {
            struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
            try {d.next_clip=reset_resolver_(d.carrier_frame,-1);}
            catch(const std::exception& exception){if(error)*error=exception.what();return {};}
            catch(...){return reject("destination carry resolver raised a non-standard exception");}
        }
        if(!d.next_clip)return reject("destination avatar default carry resource did not resolve");
    }
    if(!prepared.can_commit())return reject("World or VM identity changed during carry resource preparation");
    if(error)error->clear();return std::optional<PreparedCarryChange>{std::move(prepared)};
}
struct Playback::PreparedFloorCarryClear::Data {
    Playback* controller=nullptr;const Vm* source_vm=nullptr;const Vm* carrier_vm=nullptr;
    VmFrame source_frame,carrier_frame;Instruction instruction{};VmDispatch dispatch{};
    std::uint64_t source_run=0,source_steps=0,carrier_run=0,carrier_steps=0;
    std::int16_t item=0,carrier=0;std::uint64_t item_generation=0,carrier_generation=0;
    std::size_t slot=0;std::optional<EntityPosition> destination;
    std::shared_ptr<const AnimationHeader> previous_clip;
    std::optional<PlaybackResourceKey> previous_resource;bool consumed=false;
    bool identity() const noexcept {try{
        if(consumed||!controller||controller->executing_||!source_vm||!carrier_vm||controller->vm_!=carrier_vm||
           dispatch.origin!=source_vm||dispatch.check_mode||!dispatch.ticket||
           (instruction.opcode!=5&&instruction.opcode!=16&&instruction.opcode!=46)||
           !source_vm->matches_active_primitive(source_frame,instruction,dispatch)||
           source_vm->run_generation()!=source_run||source_vm->steps()!=source_steps||
           carrier_vm->run_generation()!=carrier_run||carrier_vm->steps()!=carrier_steps||carrier_vm->stack().empty()||
           !same_frame(carrier_frame,carrier_vm->stack().back())||
           (carrier_vm->dispatch_active()&&carrier_vm!=source_vm)||
           controller->carry_clip_!=previous_clip||!(controller->carry_resource_==previous_resource))return false;
        const auto* a=controller->world_.find(carrier);const auto* object=controller->world_.find(item);
        return a&&object&&a->kind==EntityKind::avatar&&!a->ui_ghost&&!object->ui_ghost&&
            a->generation==carrier_generation&&object->generation==item_generation&&slot<a->slots.size()&&
            carrier_frame.context.caller==carrier&&carrier_vm->stack().front().context.caller==carrier;
    }catch(...){return false;}}
};
Playback::PreparedFloorCarryClear::PreparedFloorCarryClear(std::unique_ptr<Data> data) noexcept:data_(std::move(data)){}
Playback::PreparedFloorCarryClear::PreparedFloorCarryClear(PreparedFloorCarryClear&&) noexcept=default;
Playback::PreparedFloorCarryClear& Playback::PreparedFloorCarryClear::operator=(PreparedFloorCarryClear&&) noexcept=default;
Playback::PreparedFloorCarryClear::~PreparedFloorCarryClear()=default;
bool Playback::PreparedFloorCarryClear::can_commit() const noexcept {
    if(!data_||!data_->identity())return false;
    const auto& d=*data_;const auto* item=d.controller->world_.find(d.item);const auto* actor=d.controller->world_.find(d.carrier);
    return item->attachment&&item->attachment->container==d.carrier&&item->attachment->slot==d.slot&&actor->slots[d.slot]==d.item;
}
bool Playback::PreparedFloorCarryClear::can_publish() const noexcept {
    if(!data_||!data_->identity())return false;
    const auto& d=*data_;const auto* item=d.controller->world_.find(d.item);const auto* actor=d.controller->world_.find(d.carrier);
    if(item->attachment||actor->slots[d.slot]!=0||bool(item->position)!=bool(d.destination))return false;
    if(!d.destination)return true;
    const auto& a=*item->position;const auto& b=*d.destination;
    return a.x==b.x&&a.y==b.y&&a.level==b.level&&a.direction==b.direction;
}
bool Playback::PreparedFloorCarryClear::publish() noexcept {
    if(!can_publish())return false;
    data_->controller->carry_clip_.reset();data_->controller->carry_resource_.reset();data_->consumed=true;return true;
}
std::optional<Playback::PreparedFloorCarryClear> Playback::prepare_floor_carry_clear(
    const VmFrame& frame,const Vm& source_vm,const Instruction& instruction,const VmDispatch& dispatch,
    std::int16_t item,std::uint64_t item_generation,std::optional<EntityPosition> destination,
    std::int16_t carrier,std::uint64_t carrier_generation,std::uint64_t run,std::string* error) {
    const auto reject=[&](const char* why)->std::optional<PreparedFloorCarryClear>{if(error)*error=why;return {};};
    if(executing_||!vm_||!run||vm_->run_generation()!=run||vm_->stack().empty())
        return reject("floor carry clear requires the carrier's live bound VM");
    const auto* object=world_.find(item);const auto* actor=world_.find(carrier);
    if(!object||object->generation!=item_generation||!object->attachment||object->attachment->container!=carrier||
       !actor||actor->generation!=carrier_generation||actor->person_data.size()<=58)
        return reject("floor carry clear requires the actual attached item and avatar");
    auto data=std::make_unique<PreparedFloorCarryClear::Data>();
    data->controller=this;data->source_vm=&source_vm;data->carrier_vm=vm_;data->source_frame=frame;data->carrier_frame=vm_->stack().back();
    data->instruction=instruction;data->dispatch=dispatch;data->source_run=source_vm.run_generation();data->source_steps=source_vm.steps();
    data->carrier_run=run;data->carrier_steps=vm_->steps();data->item=item;data->item_generation=item_generation;
    data->carrier=carrier;data->carrier_generation=carrier_generation;data->slot=object->attachment->slot;data->destination=destination;
    data->previous_clip=carry_clip_;data->previous_resource=carry_resource_;
    PreparedFloorCarryClear result(std::move(data));
    if(!result.can_commit())return reject("floor carry clear dispatch or attachment is stale");
    if(error)error->clear();
    return result;
}
bool Playback::start_route_animation(const VmFrame& frame,const Vm& vm,std::uint64_t actor_generation,
    std::uint64_t vm_generation,std::uint16_t index,bool swimming,float speed,bool loop,std::string* error) {
    const auto reject=[&](const char* why){if(error)*error=why;return false;};
    if(executing_||reach_executing_)return reject("route animation hook cannot reenter playback");
    if(!valid_slot_context(frame,vm,actor_generation,vm_generation,error))return false;
    if(wait_||(vm.pending()&&vm.pending()->instruction.instruction.opcode==44))
        return reject("route animation cannot replace a pending opcode44 wait");
    if(index<3||index>9||(index==3&&speed!=1.0f)||
       (speed!=1.0f&&speed!=2.0f)||loop!=(index==3))
        return reject("route animation index, speed or loop outside source profile");
    if(!route_resolver_)return reject("route animation resource resolver is unavailable");
    const auto key=resource_key(frame,*world_.frame_entity(frame,VmEntityRole::caller),PlaybackResourceKey::Kind::route_walk,
                                swimming?1:0,index);
    std::shared_ptr<const AnimationHeader> clip;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(executing_);
    try {clip=route_resolver_(frame,index,swimming);}
    catch(const std::exception& exception){if(error)*error=exception.what();return false;}
    catch(...){return reject("route animation resolver raised a non-standard exception");}
    if(!valid_slot_context(frame,vm,actor_generation,vm_generation,error))return false;
    const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
    if(actor->person_data[58]!=key.actor_age)
        return reject("route animation actor age changed during resource resolution");
    std::string why;auto next=prepare(std::move(clip),*actor,false,loop,false,why);
    if(!next){if(error)*error=why;return false;}
    next->resource=key;next->speed=speed;
    state_=std::move(*next);source_walk_.reset();
    if(!hands_)hands_=HandsState{{},actor->id,actor_generation,vm_generation};
    diagnostic_.clear();if(error)error->clear();return true;
}
// Pinned FreeSO7e76 VMRoutingFrame.StartWalkAnimation1142-1170. The source
// collection is shared by routing and opcode44, rather than cloned controllers.
bool Playback::start_source_walk_animation(const VmFrame& source_frame,const Vm& vm,
    std::uint64_t actor_generation,std::uint64_t run,bool swimming,std::int16_t style,bool pet,std::string* error){
    const auto reject=[&](const char* why){if(error)*error=why;return false;};
    if(executing_||reach_executing_)return reject("source walk animation cannot reenter playback");
    if(!valid_slot_context(source_frame,vm,actor_generation,run,error)||vm.stack().empty()||!same_frame(source_frame,vm.stack().back()))return false;
    if(wait_||(vm.pending()&&vm.pending()->instruction.instruction.opcode==44))return reject("source walk start cannot replace an opcode44 wait");
    if(!walk_name_resolver_||!route_resolver_)return reject("source walk table/resource resolver is unavailable");
    const VmFrame frame=source_frame;const auto* actor=world_.frame_entity(frame,VmEntityRole::caller);
    const auto position=actor->position;const auto attachment=actor->attachment;const auto radians=actor->radian_direction;
    const auto age=actor->person_data[58];const auto vm_status=vm.status();const auto steps=vm.steps();const auto depth=vm.stack().size();const auto pending=vm.pending();
    const auto valid=[&]{
        if(!valid_slot_context(frame,vm,actor_generation,run,error)||vm.status()!=vm_status||vm.steps()!=steps||vm.stack().size()!=depth||
           !same_frame(frame,vm.stack().back())||bool(vm.pending())!=bool(pending)||(pending&&!same_pending(*pending,*vm.pending())))return false;
        const auto* current=world_.frame_entity(frame,VmEntityRole::caller);
        return current&&current->person_data[58]==age&&bool(current->position)==bool(position)&&(!position||*current->position==*position)&&
            same_attachment(current->attachment,attachment)&&current->radian_direction==radians;
    };
    struct Guard{bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
    try{
        const auto full=static_cast<std::uint16_t>((!swimming&&style==1)?21:20);
        const auto idle_name=walk_name_resolver_(frame,3,swimming);if(!idle_name||!valid())return reject("source walk idle table entry is unknown or stale");
        const auto full_name=walk_name_resolver_(frame,full,swimming);if(!full_name||!valid())return reject("source walk full table entry is unknown or stale");
        // Intentionally compare only the first two ANIMATION NAMES. A source
        // table/profile change does not replace the existing half state here.
        if(source_walk_&&state_&&state_->clip->name==*idle_name&&source_walk_->secondary[0].clip->name==*full_name){
            if(!world_.set_static_footprint(frame.context.caller,false))return reject("source walk footprint update failed");
            diagnostic_.clear();if(error)error->clear();return true;
        }
        if(idle_name->empty()||full_name->empty())return reject("source walk idle/full table entry is empty");
        auto half=static_cast<std::uint16_t>(((!swimming&&style==1)||swimming)?21:(pet?20:25));
        auto half_name=walk_name_resolver_(frame,half,swimming);if(!half_name||!valid())return reject("source walk half table entry is unknown or stale");
        if(half_name->empty()){half=full;half_name=full_name;}
        const std::array<std::uint16_t,3> indices={3,full,half};
        std::array<State,3> prepared{};
        std::size_t queued=0,short_events=0;
        for(std::size_t lane=0;lane<3;++lane){
            const auto clip=route_resolver_(frame,indices[lane],swimming);
            if(!valid())return reject("source walk identity changed during animation loading");
            if(!clip)return reject("source walk animation resource did not resolve");
            std::string why;auto next=prepare(clip,*world_.frame_entity(frame,VmEntityRole::caller),false,true,false,why);
            if(!next){if(error)*error=why;return false;}
            next->resource=resource_key(frame,*world_.frame_entity(frame,VmEntityRole::caller),PlaybackResourceKey::Kind::source_locomotion,swimming?1:0,indices[lane]);
            next->speed=lane?1.5f:1.0f;
            for(const auto& event:next->events)if(const auto* value=property(event.pairs,"xevt")){
                if(++queued>limits_.events||((event_code(*value)<100)&&++short_events>255))return reject("source walk aggregate event budget exceeded");
            }
            prepared[lane]=std::move(*next);
        }
        if(!valid())return reject("source walk context became stale before publication");
        if(!world_.set_static_footprint(frame.context.caller,false))return reject("source walk footprint update failed");
        state_=std::move(prepared[0]);
        source_walk_=SourceWalkState{{std::move(prepared[1]),std::move(prepared[2])},{0,0.66f,0.33f}};
        if(!hands_)hands_=HandsState{{},frame.context.caller,actor_generation,run};
        diagnostic_.clear();if(error)error->clear();return true;
    }catch(const std::exception& exception){if(error)*error=exception.what();return false;}
    catch(...){return reject("source walk animation resolver failed");}
}
bool Playback::set_source_walk_velocity(const VmFrame& frame,const Vm& vm,std::uint64_t actor_generation,
    std::uint64_t run,std::int32_t velocity,std::string* error){
    if(error)error->clear();
    if(executing_||velocity<0||velocity>8||!source_walk_||!state_){if(error)*error="source walk velocity requires an existing three-state collection and speed0..8";return false;}
    if(!valid_slot_context(frame,vm,actor_generation,run,error)||vm.stack().empty()||!same_frame(frame,vm.stack().back()))return false;
    source_walk_->weights=walk_weights(velocity);return true;
}
bool Playback::end_source_walk_animation(const VmFrame& frame,const Vm& vm,std::uint64_t actor_generation,
    std::uint64_t run,std::string* error,const Instruction* lifecycle_instruction,const VmDispatch* lifecycle_dispatch){
    if(error)error->clear();
    if(executing_){if(error)*error="source walk end cannot reenter playback";return false;}
    if(!valid_slot_context(frame,vm,actor_generation,run,error,lifecycle_instruction,lifecycle_dispatch)||
       vm.stack().empty()||!same_frame(frame,vm.stack().back()))return false;
    if(source_walk_&&!state_){if(error)*error="source walk collection lacks its primary";return false;}
    if(!world_.set_static_footprint(frame.context.caller,true)){if(error)*error="source walk end footprint update failed";return false;}
    if(source_walk_){
        // Same walk/swim[3] resource binding, preserving every live cursor.
        if(state_->resource.kind==PlaybackResourceKey::Kind::source_locomotion)state_->resource.kind=PlaybackResourceKey::Kind::route_walk;
        source_walk_.reset();
    }
    return true;
}
bool Playback::capture_snapshot(PlaybackSnapshot& out,std::string* error) const {
    const auto reject=[&](const char* text){if(error)*error=text;return false;};
    if(executing_||reach_executing_)return reject("animation snapshot requested during callback/tick");
    if((wait_||source_walk_)&&!state_)return reject("animation continuation lacks its primary state");
    if(bool(carry_clip_)!=bool(carry_resource_))return reject("animation carry resource identity is missing");
    try{
        if(hands_){const auto* actor=world_.find(hands_->actor);
            if(!actor||actor->generation!=hands_->actor_generation||!vm_||hands_->vm_generation!=vm_->run_generation())
                return reject("animation hand state identity is stale");}
        PlaybackSnapshot next{};next.last_tick=last_tick_;
        const auto save=[&](const State& current)->std::optional<PlaybackClipSnapshot>{
            if(!vm_||!hands_)return {};
            const auto* actor=world_.find(current.actor);
            if(!actor||actor->kind!=EntityKind::avatar||actor->generation!=current.actor_generation||current.vm_generation!=vm_->run_generation()||
               current.resource.caller!=current.actor||current.resource.actor_age!=(actor->person_data.size()>58?actor->person_data[58]:0)||
               (!current.resource.source&&!current.resource.id&&current.resource.kind==PlaybackResourceKey::Kind::animation)||
               !std::isfinite(current.frame)||!std::isfinite(current.speed)||current.speed<=0||!current.frames||current.frames>limits_.frames||
               current.events.size()>limits_.events||current.low>current.high||current.high>current.events.size()||current.queue.size()>limits_.events||
               current.events_run>255||current.ticks>limits_.ticks||hands_->actor!=current.actor||hands_->actor_generation!=current.actor_generation||
               hands_->vm_generation!=current.vm_generation)return {};
            return PlaybackClipSnapshot{current.resource,current.actor,current.actor_generation,current.vm_generation,current.frame,current.speed,current.frames,
                current.backwards,current.end,current.loop,current.low,current.high,current.queue,current.events_run,current.ticks,{}};
        };
        if(state_){
            next.state=save(*state_);if(!next.state)return reject("animation snapshot identity or limits are stale");
            if(wait_){next.state->wait=PlaybackWaitSnapshot{wait_->ticket,wait_->bank,wait_->index,wait_->expected};
                if(!wait_matches_vm(*next.state->wait,state_->resource,state_->actor,vm_->snapshot()))return reject("animation wait differs from owning VM");}
            else if(vm_->pending()&&vm_->pending()->context.caller==state_->actor&&vm_->pending()->instruction.instruction.opcode==44)
                return reject("animation VM wait exists without playback wait state");
            if(source_walk_){
                if(!valid_walk_weights(source_walk_->weights))return reject("source walk weights are outside the source velocity profile");
                PlaybackSourceWalkSnapshot collection;collection.weights=source_walk_->weights;
                for(std::size_t lane=0;lane<2;++lane){const auto saved=save(source_walk_->secondary[lane]);
                    if(!saved)return reject("source walk secondary animation identity is stale");
                    collection.secondary[lane]=*saved;}
                const auto& a=collection.secondary[0];const auto& b=collection.secondary[1];
                if(!locomotion_key(a.resource)||!locomotion_key(b.resource)||!same_resource_context(a.resource,b.resource)||
                   (a.resource.id!=20&&a.resource.id!=21)||(a.resource.source==1&&(a.resource.id!=20||b.resource.id!=21))||
                   (a.resource.source==0&&((a.resource.id==21&&b.resource.id!=21)||(a.resource.id==20&&b.resource.id!=20&&b.resource.id!=25)))||
                   (state_->resource.kind==PlaybackResourceKey::Kind::source_locomotion&&
                    (!same_resource_context(state_->resource,a.resource)||state_->resource.id!=3))||
                   (state_->resource.kind!=PlaybackResourceKey::Kind::source_locomotion&&state_->resource.kind!=PlaybackResourceKey::Kind::animation)||
                   state_->speed!=1||state_->backwards||state_->end||!state_->loop)
                    return reject("source walk collection resource profile is invalid");
                for(const auto& lane:collection.secondary)if(lane.actor!=state_->actor||lane.actor_generation!=state_->actor_generation||
                    lane.vm_generation!=state_->vm_generation||lane.speed!=1.5f||lane.backwards||lane.end||!lane.loop||lane.wait||
                    !lane.queue.empty()||lane.events_run||lane.ticks!=state_->ticks)return reject("source walk secondary continuation is invalid");
                next.source_walk=std::move(collection);
            }else if(state_->resource.kind==PlaybackResourceKey::Kind::source_locomotion)return reject("locomotion primary lacks its collection");
        }else if(vm_&&vm_->pending()&&vm_->pending()->instruction.instruction.opcode==44)
            return reject("animation VM wait exists without playback state");
        if(carry_clip_)next.carry=carry_resource_;
        if(hands_)next.hands=PlaybackHandsSnapshot{hands_->value,hands_->actor,hands_->actor_generation,hands_->vm_generation};
        out=std::move(next);if(error)error->clear();return true;
    }catch(const std::exception& exception){if(error)*error=exception.what();return false;}
}
bool Playback::restore_snapshot(const PlaybackSnapshot& saved,Vm& vm,const PlaybackRestoreResolver& resolver,std::string* error) {
    const auto reject=[&](const char* text){if(error)*error=text;return false;};
    if(executing_||reach_executing_||vm.dispatch_active()||(vm_&&vm_!=&vm))return reject("animation restore callback or VM identity is invalid");
    if(saved.source_walk&&!saved.state)return reject("source walk snapshot lacks its primary");
    const auto fail_key=[](const PlaybackResourceKey& key){
        if(key.kind==PlaybackResourceKey::Kind::reach)return key.source!=0||key.id>3||key.posture!=0;
        if(key.kind==PlaybackResourceKey::Kind::source_locomotion)return !locomotion_key(key);
        return (key.kind!=PlaybackResourceKey::Kind::animation&&key.kind!=PlaybackResourceKey::Kind::posture_reset&&key.kind!=PlaybackResourceKey::Kind::route_walk)||
            (key.kind==PlaybackResourceKey::Kind::route_walk&&(key.source>1||key.id<3||key.id>9||key.posture!=0))||
            (key.kind==PlaybackResourceKey::Kind::animation&&((!key.source&&!key.id)||key.posture!=0))||
            (key.kind==PlaybackResourceKey::Kind::posture_reset&&(key.source||key.id||(key.posture!=-1&&key.posture!=1&&key.posture!=2&&key.posture!=3)));
    };
    const auto original_vm=vm_;
    struct BindingRollback{Vm*& binding;Vm* old;bool committed=false;~BindingRollback(){if(!committed)binding=old;}} binding{vm_,original_vm};
    vm_=&vm;
    struct Guard{bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
    try{
        const auto run=vm.run_generation(),steps=vm.steps();const auto frames=vm.stack();const auto pending=vm.pending();const auto vm_status=vm.status();
        const auto vm_valid=[&]{
            if(vm.run_generation()!=run||vm.steps()!=steps||vm.status()!=vm_status||vm.stack().size()!=frames.size()||bool(vm.pending())!=bool(pending)||
               (pending&&!same_pending(*pending,*vm.pending())))return false;
            for(std::size_t n=0;n<frames.size();++n)if(!same_frame(frames[n],vm.stack()[n]))return false;
            return true;
        };
        const auto actor_valid=[&](const PlaybackClipSnapshot& s){const auto* actor=world_.find(s.actor);
            return actor&&actor->kind==EntityKind::avatar&&actor->generation==s.actor_generation&&s.resource.caller==s.actor&&
                s.resource.actor_age==(actor->person_data.size()>58?actor->person_data[58]:0)&&s.vm_generation==run;
        };
        const auto load=[&](const PlaybackClipSnapshot& s,bool secondary)->std::optional<State>{
            if(!saved.hands||fail_key(s.resource)||(s.resource.kind==PlaybackResourceKey::Kind::posture_reset&&s.resource.posture==-1)||
               !actor_valid(s)||!std::isfinite(s.frame)||!std::isfinite(s.speed)||s.speed<=0||!s.frames||s.frames>limits_.frames||s.ticks>limits_.ticks||
               s.queue.size()>limits_.events||s.events_run>255||s.low>s.high||!resolver)return {};
            if(s.resource.kind==PlaybackResourceKey::Kind::route_walk&&(s.wait||s.backwards||s.loop!=(s.resource.id==3)||
                (s.speed!=1&&s.speed!=2)||(s.resource.id==3&&s.speed!=1)))return {};
            if(s.resource.kind==PlaybackResourceKey::Kind::reach&&(s.wait||s.backwards||s.loop||s.speed!=1.0f))return {};
            if(s.resource.kind==PlaybackResourceKey::Kind::source_locomotion&&(!saved.source_walk||s.wait||s.backwards||s.end||!s.loop||
                (secondary?(s.speed!=1.5f||!s.queue.empty()||s.events_run):(s.resource.id!=3||s.speed!=1))))return {};
            const auto clip=resolver(s.resource);if(!clip||!vm_valid()||!actor_valid(s))return {};
            std::string why;auto base=prepare(clip,*world_.find(s.actor),s.backwards,s.loop,false,why);if(!base)return {};
            const auto integral=std::trunc(static_cast<double>(s.frame));
            const bool bad_frame=(s.end&&s.loop)||(!s.loop&&(s.end?(s.backwards?integral>=0:integral<s.frames):
                (s.backwards?(integral<0||s.frame>static_cast<float>(s.frames)):(s.frame<0||integral>=s.frames))));
            if(base->frames!=s.frames||s.low>base->events.size()||s.high>base->events.size()||
                (s.backwards?s.low!=0:s.high!=base->events.size())||static_cast<double>(s.frame)<std::numeric_limits<std::int32_t>::min()||
                static_cast<double>(s.frame)>std::numeric_limits<std::int32_t>::max()||bad_frame)return {};
            base->resource=s.resource;base->frame=s.frame;base->speed=s.speed;base->end=s.end;base->low=s.low;base->high=s.high;
            base->queue=s.queue;base->events_run=s.events_run;base->ticks=s.ticks;return base;
        };
        std::optional<State> restored;std::optional<Wait> restored_wait;std::optional<SourceWalkState> collection;
        if(saved.state){restored=load(*saved.state,false);if(!restored)return reject("animation snapshot state, resource, cursor or identity is invalid");
            if(saved.state->wait){const auto& wait=*saved.state->wait;
                if((wait.bank!=VmFrameBank::arguments&&wait.bank!=VmFrameBank::locals)||!wait_matches_vm(wait,saved.state->resource,saved.state->actor,vm.snapshot()))
                    return reject("animation wait does not match restored VM pending call");
                restored_wait=Wait{wait.ticket,wait.bank,wait.index,wait.expected};
            }else if(vm.pending()&&vm.pending()->context.caller==restored->actor&&vm.pending()->instruction.instruction.opcode==44)
                return reject("restored animation VM wait has no playback controller wait");
        }else if(vm.pending()&&vm.pending()->instruction.instruction.opcode==44)return reject("restored animation VM wait lacks its state");
        if(saved.source_walk){
            const auto& walk=*saved.source_walk;const auto& a=walk.secondary[0];const auto& b=walk.secondary[1];
            if(!valid_walk_weights(walk.weights)||!locomotion_key(a.resource)||!locomotion_key(b.resource)||!same_resource_context(a.resource,b.resource)||
                (a.resource.id!=20&&a.resource.id!=21)||(a.resource.source==1&&(a.resource.id!=20||b.resource.id!=21))||
                (a.resource.source==0&&((a.resource.id==21&&b.resource.id!=21)||(a.resource.id==20&&b.resource.id!=20&&b.resource.id!=25)))||
                (restored->resource.kind!=PlaybackResourceKey::Kind::source_locomotion&&restored->resource.kind!=PlaybackResourceKey::Kind::animation)||
                restored->backwards||restored->end||!restored->loop||restored->speed!=1)return reject("source walk snapshot profile is invalid");
            if(restored->resource.kind==PlaybackResourceKey::Kind::source_locomotion){
                if(!same_resource_context(restored->resource,a.resource)||restored->resource.id!=3)return reject("source walk primary resource differs from its collection");
            }else{
                auto idle_key=a.resource;idle_key.id=3;const auto idle=resolver(idle_key);
                if(!vm_valid()||!actor_valid(*saved.state)||!idle||idle!=restored->clip)
                    return reject("opcode44 primary is not the exact immutable source-walk idle alias");
            }
            auto full=load(a,true),half=load(b,true);if(!full||!half)return reject("source walk secondary snapshot is invalid");
            for(const auto* lane:{&*full,&*half})if(lane->actor!=restored->actor||lane->actor_generation!=restored->actor_generation||
                lane->vm_generation!=restored->vm_generation||lane->ticks!=restored->ticks)return reject("source walk actor/run/tick identities disagree");
            collection=SourceWalkState{{std::move(*full),std::move(*half)},walk.weights};
        }else if(restored&&restored->resource.kind==PlaybackResourceKey::Kind::source_locomotion)return reject("source locomotion primary lacks its collection");
        std::shared_ptr<const AnimationHeader> carry;std::optional<std::uint64_t> carry_generation;
        if(saved.carry){
            if(fail_key(*saved.carry)||(saved.carry->kind!=PlaybackResourceKey::Kind::animation&&
                !(saved.carry->kind==PlaybackResourceKey::Kind::posture_reset&&saved.carry->posture==-1))||!resolver)return reject("animation carry key is invalid");
            const auto* actor=world_.find(saved.carry->caller);
            if(!actor||actor->kind!=EntityKind::avatar||saved.carry->actor_age!=(actor->person_data.size()>58?actor->person_data[58]:0))return reject("animation carry actor context is stale");
            carry_generation=actor->generation;
            carry=resolver(*saved.carry);if(!carry||!vm_valid())return reject("animation carry resource did not resolve safely");
        }
        if(saved.hands){const auto& h=*saved.hands;const auto* actor=world_.find(h.actor);
            if(!valid_hand_gesture(h.value.left)||!valid_hand_gesture(h.value.right)||!actor||actor->kind!=EntityKind::avatar||actor->generation!=h.actor_generation||h.vm_generation!=run)
                return reject("animation hand snapshot identity or value is invalid");
            if(restored&&(h.actor!=restored->actor||h.actor_generation!=restored->actor_generation||h.vm_generation!=restored->vm_generation))return reject("animation state and hands identities disagree");
        }
        if(!vm_valid()||(saved.state&&!actor_valid(*saved.state)))return reject("animation context changed during snapshot resource loading");
        if(saved.carry){const auto* owner=world_.find(saved.carry->caller);
            if(!owner||owner->kind!=EntityKind::avatar||owner->generation!=*carry_generation||saved.carry->actor_age!=(owner->person_data.size()>58?owner->person_data[58]:0))
                return reject("animation carry actor changed during resource loading");}

        if(saved.source_walk)for(const auto& lane:saved.source_walk->secondary)if(!actor_valid(lane))return reject("source walk actor identity changed during restore");
        state_=std::move(restored);source_walk_=std::move(collection);wait_=restored_wait;carry_clip_=std::move(carry);carry_resource_=saved.carry;
        hands_.reset();if(saved.hands)hands_=HandsState{saved.hands->value,saved.hands->actor,saved.hands->actor_generation,saved.hands->vm_generation};
        last_tick_=saved.last_tick;diagnostic_.clear();vm_=&vm;binding.committed=true;if(error)error->clear();return true;
    }catch(const std::exception& exception){if(error)*error=exception.what();return false;}
    catch(...){return reject("animation restore resolver raised a non-standard exception");}
}
std::optional<AvatarHands> Playback::hands() const {
    if(!hands_||!vm_||hands_->vm_generation!=vm_->run_generation())return {};
    const auto* actor=world_.find(hands_->actor);
    if(!actor||actor->generation!=hands_->actor_generation)return {};
    return hands_->value;
}
VmPrimitive Playback::primitive() {return [this](const auto& f,const auto& i,const auto&,const auto& d){return request(f,i,d);};}
std::optional<PlaybackSample> Playback::sample() const {
    if(source_walk_||!state_ || !vm_ || state_->vm_generation!=vm_->run_generation())return {};
    const auto* actor=world_.find(state_->actor);
    if(!actor || actor->generation!=state_->actor_generation)return {};
    if(wait_ && (!vm_->pending() || vm_->pending()->ticket!=wait_->ticket))return {};
    return PlaybackSample{state_->clip,state_->actor,state_->actor_generation,state_->vm_generation,
                          state_->frame,state_->frames,state_->end,state_->loop,hands().value_or(AvatarHands{})};
}
std::optional<PlaybackSampleCollection> Playback::sample_collection() const {
    if(!source_walk_){const auto single=sample();if(!single)return {};PlaybackSampleCollection value;value.count=1;value.layers[0]={*single,1};return value;}
    if(!state_||!vm_||state_->vm_generation!=vm_->run_generation()||!valid_walk_weights(source_walk_->weights))return {};
    const auto* actor=world_.find(state_->actor);if(!actor||actor->generation!=state_->actor_generation)return {};
    if(wait_&&(!vm_->pending()||vm_->pending()->ticket!=wait_->ticket))return {};
    PlaybackSampleCollection value;value.count=3;
    for(std::size_t lane=0;lane<3;++lane){const auto& current=lane?source_walk_->secondary[lane-1]:*state_;
        if(current.actor!=state_->actor||current.actor_generation!=state_->actor_generation||current.vm_generation!=state_->vm_generation)return {};
        value.layers[lane]={PlaybackSample{current.clip,current.actor,current.actor_generation,current.vm_generation,current.frame,current.frames,
            current.end,current.loop,hands().value_or(AvatarHands{})},source_walk_->weights[lane]};
    }
    return value;
}
std::optional<PlaybackProgress> Playback::progress() const {
    if(!state_)return {};
    return PlaybackProgress{state_->clip->name,state_->frame,state_->frames,state_->queue.size(),state_->end,state_->loop};
}
std::optional<Playback::State> Playback::prepare(std::shared_ptr<const AnimationHeader> clip,const Entity& actor,
        bool backwards,bool loop,bool hurryable,std::string& error) const {
    const auto reject=[&](const std::string& text)->std::optional<State>{error=text;return {};};
    if(!clip)return reject("animation resource not supplied by explicit resolver");
    State next{};next.clip=std::move(clip);next.actor=actor.id;next.actor_generation=actor.generation;
    next.vm_generation=vm_->run_generation();next.backwards=backwards;next.loop=loop;next.speed=30.0f/25.0f;
    if(hurryable){const auto walk=actor.object_words.find(17);
        if(walk==actor.object_words.end())return reject("hurryable animation requires explicit WalkStyle");
        if(walk->second==1)next.speed*=2;}
    std::size_t counted=0,pairs=0,counted_short_events=0;
    for(const auto& motion:next.clip->motions){next.frames=std::max(next.frames,motion.frames);
        for(const auto& group:motion.time_properties)for(const auto& event:group){
            if(next.events.size()>=limits_.events)return reject("animation event budget exceeded");
            for(const auto& pair:event.pairs){
                if(pairs>=limits_.events)return reject("animation property budget exceeded");
                ++pairs;
                // The pinned avatar handler queries known keys; other metadata
                // (including original clips' "foo") has no simulation effect.
                // Dress/undress publish exact source keys and visual assets. Sound
                // is an optional observer effect and does not block silent playback.
                if((pair.first=="dress"||pair.first=="undress")&&(pair.second.size()>252||
                    std::any_of(pair.second.begin(),pair.second.end(),[](unsigned char c){return !std::isalnum(c)&&c!='-'&&c!='_'&&c!='.';})))
                    return reject("animation appearance event is not a bounded flat asset name");
            }
            if(const auto* value=property(event.pairs,"xevt")){
                if(counted>=limits_.events)return reject("animation queue budget exceeded");
                ++counted;
                if(event_code(*value)<100&&++counted_short_events>255)return reject("animation event counter exceeds byte profile");
            }
            for(const auto* key:{"righthand","lefthand"})if(const auto* value=property(event.pairs,key))
                if(!gesture(*value))return reject("animation hand gesture outside supported numeric range 0..3");
            next.events.push_back(event);
        }}
    if(!next.frames||next.frames>limits_.frames||next.frames>static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())||!limits_.ticks)return reject("animation frame/tick budget exceeded");
    next.frame=next.backwards?static_cast<float>(next.frames):0;next.high=next.events.size();return next;
}
// VMReach port derived from FreeSO VMReach.cs at7e76c1960252ddd033d8937d5edb47d326f5b69c (MPL-2.0).
VmPrimitive Playback::reach_primitive(ReachServices services) {
    return [this,services=std::move(services)](const VmFrame& frame,const Instruction& instruction,
            const Ts1Temps&,const VmDispatch& dispatch) {
        return reach(frame,instruction,dispatch,services);
    };
}
PrimitiveOutcome Playback::reach(const VmFrame& f,const Instruction& i,const VmDispatch& d,const ReachServices& services) {
    if(i.opcode!=47)return {};
    if(reach_executing_)return unsupported("Reach callbacks cannot reenter Reach");
    struct ReachGuard {bool& value;explicit ReachGuard(bool& v):value(v){value=true;}~ReachGuard(){value=false;}} reach_guard(reach_executing_);
    const auto live=[&]{return !executing_&&vm_&&d.origin==vm_&&!d.check_mode&&
        vm_->matches_active_primitive(f,i,d);};
    if(!live())return unsupported("Reach requires its actual live opcode47 dispatch");
    if(state_&&state_->vm_generation!=vm_->run_generation()){state_.reset();source_walk_.reset();wait_.reset();}
    if(wait_)return unsupported("Reach cannot replace a pending animation wait");
    const auto mode=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
    const auto parameter=static_cast<std::uint16_t>(i.operands[4]|(i.operands[5]<<8));
    if(mode>1)return unsupported("Reach to mouth is not implemented in the source");
    auto* actor=world_.find(f.context.caller);const auto* target=world_.find(f.context.stack_object);
    if(!actor||actor->kind!=EntityKind::avatar||actor->ui_ghost||!target||target->ui_ghost||!containment_slot_value(*actor,0))
        return unsupported("Reach requires live avatar, StackObject and hand slot0");
    const auto actor_generation=actor->generation,target_generation=target->generation,run=vm_->run_generation();
    const auto identities=[&]{const auto* a=world_.find(f.context.caller);const auto* t=world_.find(f.context.stack_object);
        return live()&&vm_->run_generation()==run&&a&&t&&a->generation==actor_generation&&t->generation==target_generation;};
    const bool completed_pickup=mode==0&&target->attachment&&target->attachment->container==actor->id;
    float raw_height=0;
    std::size_t slot=0;
    try{
        if(mode==0){
            if(target->attachment){const auto* container=world_.find(target->attachment->container);
                if(!container)return unsupported("Reach container is absent");
                if(container->kind!=EntityKind::avatar){
                    if(!services.slot_height)return unsupported("Reach SLOT height resolver is unavailable");
                    raw_height=services.slot_height(container->id,target->attachment->slot).value_or(0);
                }
            }
        }else{
            if(parameter>=f.arguments.size())return unsupported("Reach SlotParam is outside arguments");
            if(f.arguments[parameter]<0)return unsupported("Reach slot index is negative");
            slot=static_cast<std::size_t>(f.arguments[parameter]);
            if(!services.slot_height)return unsupported("Reach SLOT height resolver is unavailable");
            const auto height=services.slot_height(target->id,slot);
            if(!height)return {PrimitiveDisposition::goto_false,{}};
            raw_height=*height;
        }
    }catch(const std::exception& error){return unsupported(error.what());}
    if(!identities())return unsupported("Reach identities changed during SLOT resolution");
    if(!std::isfinite(raw_height))return unsupported("Reach height is not finite");
    // Math.Round(float) promotes to double and rounds ties to even.
    const auto floor_height=std::floor(static_cast<double>(raw_height));
    const auto remainder=static_cast<double>(raw_height)-floor_height;
    const auto height=floor_height+((remainder>0.5||(remainder==0.5&&std::fmod(floor_height,2.0)!=0))?1.0:0.0);
    actor=world_.find(f.context.caller);target=world_.find(f.context.stack_object);
    const auto profile=static_cast<std::uint16_t>(actor->attachment?0:(height<2?1:(height<4?2:3)));
    std::shared_ptr<const AnimationHeader> clip;
    try{if(services.animation)clip=services.animation(profile);}
    catch(const std::exception& error){return unsupported(error.what());}
    if(!identities())return unsupported("Reach identities changed during animation resolution");
    if(!clip)return {PrimitiveDisposition::goto_false,{}};
    actor=world_.find(f.context.caller);target=world_.find(f.context.stack_object);
    if(!state_||(state_->clip!=clip&&!completed_pickup)){
        std::string error;auto next=prepare(clip,*actor,false,false,false,error);
        if(!next)return unsupported(error);
        next->speed=1.0f; // VMReach uses VMAnimationState's default, not AnimateSim's TS1 speed.
        next->resource=resource_key(f,*actor,PlaybackResourceKey::Kind::reach,0,profile);
        state_=std::move(*next);source_walk_.reset();
        hands_=HandsState{{},actor->id,actor_generation,run};
        return {PrimitiveDisposition::continue_next_tick,{}};
    }
    if(state_->actor!=actor->id||state_->actor_generation!=actor_generation||state_->vm_generation!=run)
        return unsupported("Reach primary animation identity is stale");
    // Source Reach deliberately checks end BEFORE consuming an event, unlike44.
    if(state_->end){state_.reset();source_walk_.reset();return {PrimitiveDisposition::goto_true,{}};}
    if(!state_->queue.empty()){
        if(state_->queue.front()==0){
            std::int16_t item=0,container=0;std::size_t destination_slot=0;
            const auto holding=containment_slot_value(*actor,0);
            if(!holding)return unsupported("Reach hand slot is outside World");
            if(mode==0){if(*holding==0){item=target->id;container=actor->id;}}
            else{
                const auto stored=containment_slot_value(*target,slot);
                if(!stored)return unsupported("Reach storage slot is outside World");
                if(*holding==0){item=*stored;container=actor->id;}
                else if(*stored==0){item=*holding;container=target->id;destination_slot=slot;}
            }
            if(item){
                if(!services.transfer)return unsupported("Reach slot transfer service is unavailable");
                std::string error;std::optional<bool> moved;
                const auto clip_before=state_->clip;const auto key_before=state_->resource;
                const auto frame_before=state_->frame;const auto ticks_before=state_->ticks;
                const auto low_before=state_->low,high_before=state_->high,queue_before=state_->queue.size();
                try{moved=services.transfer(f,i,d,item,container,destination_slot,error);}
                catch(const std::exception& exception){return unsupported(exception.what());}
                if(!moved)return unsupported(error.empty()?"Reach slot transfer unsupported":error);
                if(!identities()||!state_||state_->clip!=clip_before||!(state_->resource==key_before)||
                   state_->frame!=frame_before||state_->ticks!=ticks_before||state_->low!=low_before||state_->high!=high_before||
                   state_->queue.size()!=queue_before||state_->queue.empty()||state_->queue.front()!=0)
                    return unsupported("Reach primary state or identities changed during slot transfer");
            }
        }
        state_->queue.pop_front();
    }
    return {PrimitiveDisposition::continue_next_tick,{}};
}
PrimitiveOutcome Playback::request(const VmFrame& f,const Instruction& i,const VmDispatch& d) {
    if(i.opcode!=44)return {};
    if(executing_||reach_executing_||!vm_||d.origin!=vm_)return unsupported("animation controller identity/reentry rejected");
    diagnostic_.clear();
    if(state_&&state_->vm_generation!=vm_->run_generation()){state_.reset();source_walk_.reset();wait_.reset();}
    const auto flags=i.operands[5];auto mode=(flags&1U)|((flags>>3)&2U);
    if(flags&0x80U)return unsupported("animation flags outside playback profile");
    auto id=static_cast<std::uint16_t>(i.operands[0]|(i.operands[1]<<8));
    std::uint32_t source=i.operands[4];
    if(flags&4U){if(id>=f.arguments.size())return unsupported("animation parameter index outside frame");
        id=static_cast<std::uint16_t>(f.arguments[id]);if(source==0)source=65536;}
    const auto* actor=world_.find(f.context.caller);
    if(!actor||actor->kind!=EntityKind::avatar)return unsupported("animation requires avatar caller");
    if(wait_)return unsupported("animation already has a pending wait");
    if(id==0){
        if(mode==3){carry_clip_.reset();carry_resource_.reset();return {PrimitiveDisposition::goto_true,{}};}
        if(actor->slots.empty())return unsupported("animation reset requires avatar hand slot zero");
        // Simitone resets posture and HeadSeekState while preserving a held object's carry loop.
        if(actor->person_data.size()<=42)return unsupported("animation reset requires Posture and HeadSeekState person data");
        if(!reset_resolver_&&!reset_lookup_resolver_)return unsupported("animation reset resource resolver not supplied");
        const auto posture=actor->person_data[0];
        const auto normalized=static_cast<std::int16_t>((posture==1||posture==2)?posture:3);
        std::shared_ptr<const AnimationHeader> reset_clip;
        if(reset_lookup_resolver_){
            const auto lookup=reset_lookup_resolver_(f,normalized);
            if(lookup.availability==ResetAnimationAvailability::absent){
                if(lookup.clip)return unsupported("absent reset animation unexpectedly carries a clip");
                // Pinned VMAnimateSim reset clears primary Animations before
                // its missing-clip early TRUE. Hands/carry are after that exit
                // and must retain their prior owned state; invent no pose.
                if(actor->person_data[42]==1)world_.set_person_data(actor->id,42,4);
                state_.reset();source_walk_.reset();
                return {PrimitiveDisposition::goto_true,{}};
            }
            if(lookup.availability!=ResetAnimationAvailability::available||!lookup.clip)
                return unsupported(lookup.diagnostic.empty()?"reset animation source authority unavailable":lookup.diagnostic);
            reset_clip=lookup.clip;
        }else reset_clip=reset_resolver_(f,normalized);
        std::string error;auto next=prepare(std::move(reset_clip),*actor,(flags&2U)!=0,true,false,error);
        if(!next)return unsupported(error);
        std::shared_ptr<const AnimationHeader> next_carry;
        if(actor->slots[0]!=0&&!carry_clip_){
            if(!reset_resolver_)return unsupported("default carry animation resource resolver unavailable");
            next_carry=reset_resolver_(f,-1);
            if(!next_carry)return unsupported("default carry animation resource not supplied");
        }
        // Commit only after resource and budget validation.
        if(actor->slots[0]==0){carry_clip_.reset();carry_resource_.reset();}
        else if(next_carry){carry_clip_=std::move(next_carry);
            carry_resource_=resource_key(f,*actor,PlaybackResourceKey::Kind::posture_reset,0,0,-1);}
        if(actor->person_data[42]==1)world_.set_person_data(actor->id,42,4);
        next->resource=resource_key(f,*actor,PlaybackResourceKey::Kind::posture_reset,0,0,normalized);
        state_=std::move(*next);source_walk_.reset();hands_=HandsState{{},actor->id,actor->generation,vm_->run_generation()};
        return {PrimitiveDisposition::goto_true,{}};
    }
    std::shared_ptr<const AnimationHeader> clip;
    if(animation_lookup_resolver_){
        const auto lookup=animation_lookup_resolver_(f,source,id);
        if(lookup.availability==AnimationAvailability::absent){
            if(lookup.clip)return unsupported("absent animation unexpectedly carries a clip");
            // Pinned VMAnimateSim exits before its mode switch: no output,
            // event, wait, primary/carry or gesture replacement on this path.
            return {PrimitiveDisposition::goto_true_next_tick,{}};
        }
        if(lookup.availability!=AnimationAvailability::available||!lookup.clip)
            return unsupported(lookup.diagnostic.empty()?"animation source authority unavailable":lookup.diagnostic);
        clip=lookup.clip;
    }else{
        clip=resolver_?resolver_(f,source,id):nullptr;
        if(!clip)return unsupported(mode==2?"carry animation resource not supplied by explicit resolver":
            "animation resource not supplied by explicit resolver");
    }
    if(mode==3){carry_clip_.reset();carry_resource_.reset();}
    if(mode==2){
        carry_clip_=clip;carry_resource_=resource_key(f,*actor,PlaybackResourceKey::Kind::animation,source,id);
        return {PrimitiveDisposition::goto_true,{}};
    }
    const auto bank=(flags&32U)?VmFrameBank::locals:VmFrameBank::arguments;
    const auto index=static_cast<std::size_t>((flags&32U)?i.operands[2]:0);
    if(mode==0&&bank==VmFrameBank::arguments&&index>=f.arguments.size())return unsupported("animation event destination outside frame");
    if(mode==1||!state_||state_->clip!=clip||state_->actor!=actor->id||state_->actor_generation!=actor->generation) {
        std::string error;auto next=prepare(clip,*actor,(flags&2U)!=0,mode==1,(flags&64U)!=0,error);
        if(!next)return unsupported(error);
        next->resource=resource_key(f,*actor,PlaybackResourceKey::Kind::animation,source,id);
        state_=std::move(*next);source_walk_.reset();
        hands_=HandsState{{},actor->id,actor->generation,vm_->run_generation()};
    }
    if(mode==1)return {PrimitiveDisposition::goto_true,{}};
    // Same immutable clip can be reused by another frame/resource alias. Keep
    // its avatar state, but bind this wait/event access to its exact request.
    state_->resource=resource_key(f,*actor,PlaybackResourceKey::Kind::animation,source,id);
    // On reentry, queued events take precedence over the end marker.
    if(state_->end)while(state_->events_run<i.operands[6]){
        if(state_->queue.size()>=limits_.events)return unsupported("animation queue budget exceeded");
        state_->queue.push_back(static_cast<std::int16_t>(state_->events_run++));}
    if(!state_->queue.empty()){const auto value=state_->queue.front();state_->queue.pop_front();
        return {PrimitiveDisposition::goto_false,{},VmFrameWrite{bank,index,value,bank==VmFrameBank::locals}};}
    if(state_->end){state_.reset();source_walk_.reset();return {PrimitiveDisposition::goto_true,{}};}
    wait_=Wait{d.ticket,bank,index,i.operands[6]};return {PrimitiveDisposition::wait,{}};
}
bool Playback::deliver() {
    if(!wait_||!state_)return false;
    if(state_->end)while(state_->events_run<wait_->expected){
        if(state_->queue.size()>=limits_.events){const auto ticket=wait_->ticket;wait_.reset();state_.reset();source_walk_.reset();
            vm_->complete(ticket,false);diagnostic_="animation queue budget exceeded";return true;}
        state_->queue.push_back(static_cast<std::int16_t>(state_->events_run++));}
    if(!state_->queue.empty()){
        const auto value=state_->queue.front();
        if(!vm_->complete(wait_->ticket,false,VmFrameWrite{wait_->bank,wait_->index,value,
                wait_->bank==VmFrameBank::locals}))return false;
        state_->queue.pop_front();wait_.reset();return true;
    }
    if(state_->end){const auto accepted=vm_->complete(wait_->ticket,true);state_.reset();source_walk_.reset();wait_.reset();return accepted;}
    return false;
}
bool Playback::discard_for_source_reset(){
    if(executing_||reach_executing_||(vm_&&vm_->dispatch_active()))return false;
    wait_.reset();state_.reset();source_walk_.reset();diagnostic_.clear();return true;
}
bool Playback::finish_source_reset(){
    if(executing_||reach_executing_||!vm_||vm_->dispatch_active()||state_||wait_||source_walk_)return false;
    if(hands_){const auto* actor=world_.find(hands_->actor);
        if(!actor||actor->generation!=hands_->actor_generation)return false;
        hands_->vm_generation=vm_->run_generation();}
    return true;
}
void Playback::cancel() {
    if(executing_||reach_executing_)throw std::logic_error("animation callbacks must not reenter cancellation");
    const auto ticket=wait_?std::optional<std::uint64_t>(wait_->ticket):std::nullopt;
    wait_.reset();state_.reset();source_walk_.reset();diagnostic_="animation canceled by host";
    if(ticket&&vm_)vm_->complete(*ticket,false);
}
bool Playback::tick(std::uint64_t now,const std::function<void(const PlaybackEvent&)>& observed) {
    return tick_impl(now,observed,true,false);
}
bool Playback::advance(std::uint64_t now,const std::function<void(const PlaybackEvent&)>& observed) {
    return tick_impl(now,observed,false,true);
}
bool Playback::tick_impl(std::uint64_t now,const std::function<void(const PlaybackEvent&)>& observed,
        bool deliver_pending,bool report_success) {
    if(executing_||reach_executing_)throw std::logic_error("animation ticks must not reenter");
    struct Guard{bool& value;explicit Guard(bool& v):value(v){value=true;}~Guard(){value=false;}} guard(executing_);
    if((report_success&&vm_&&vm_->dispatch_active())||(last_tick_&&(now<=*last_tick_||now-*last_tick_!=1)))return false;
    last_tick_=now;if(!vm_)return false;
    if(!state_)return report_success&&!source_walk_&&!wait_&&(!vm_->pending()||vm_->pending()->instruction.instruction.opcode!=44);
    const auto failed=[&](const char* why){
        diagnostic_=why;if(report_success)return false;
        const auto ticket=wait_?std::optional<std::uint64_t>{wait_->ticket}:std::nullopt;
        state_.reset();source_walk_.reset();wait_.reset();if(ticket)vm_->complete(*ticket,false);return true;
    };
    if(state_->vm_generation!=vm_->run_generation()||(wait_&&(!vm_->pending()||vm_->pending()->ticket!=wait_->ticket))){
        state_.reset();source_walk_.reset();wait_.reset();diagnostic_="animation canceled by VM restart/completion";return false;
    }
    const auto* actor=world_.find(state_->actor);
    if(!actor||actor->generation!=state_->actor_generation||state_->ticks>=limits_.ticks)return failed("animation entity changed or tick budget exceeded");
    if(source_walk_){
        if(!valid_walk_weights(source_walk_->weights))return failed("source walk weights are invalid");
        for(const auto& lane:source_walk_->secondary)if(lane.actor!=state_->actor||lane.actor_generation!=state_->actor_generation||
            lane.vm_generation!=state_->vm_generation||lane.ticks>=limits_.ticks)return failed("source walk secondary identity or tick budget is stale");
    }
    bool changed=deliver_pending?deliver():false;
    if(!state_){source_walk_.reset();return report_success||changed;}
    std::vector<PlaybackEvent> notifications;
    const auto advance_one=[&](State& current,float weight,std::uint8_t layer)->const char* {
        ++current.ticks;
        // Pinned VMAvatar.Tick: weight zero pauses BOTH frames and properties.
        // SilentFrameProgress below still executes for every collection state.
        if(!current.end&&weight!=0){
            current.frame+=current.backwards?-current.speed:current.speed;
            const auto numeric=static_cast<double>(current.frame);
            if(!std::isfinite(numeric)||numeric<std::numeric_limits<std::int32_t>::min()||numeric>std::numeric_limits<std::int32_t>::max())return "animation frame range exceeded";
            const auto time=(current.frame*1000.0f)/30.0f;
            while(current.low<current.high){const auto index=current.backwards?current.high-1:current.low;
                const auto& event=current.events[index];const auto at=static_cast<float>(event.id);
                if(current.backwards?at<time:at>time)break;
                if(current.backwards)--current.high;else ++current.low;
                // Source HandleTimePropsEvent always writes CurrentAnimationState,
                // including events raised by the full/half secondary animations.
                if(const auto* value=property(event.pairs,"xevt")){
                    if(state_->queue.size()>=limits_.events)return "animation queue budget exceeded";
                    const auto code=event_code(*value);
                    if(code<100&&state_->events_run>=255)return "animation event counter exceeds byte profile";
                    state_->queue.push_back(code);if(code<100)++state_->events_run;
                }
                if(const auto* value=property(event.pairs,"dress"))
                    if(!world_.add_bound_appearance(current.actor,*value,*value+".apr"))return "animation dress publication failed";
                if(const auto* value=property(event.pairs,"undress"))
                    if(!world_.remove_bound_appearance(current.actor,*value,*value+".apr"))return "animation undress publication failed";
                if(const auto* value=property(event.pairs,"righthand"))hands_->value.right=*gesture(*value);
                if(const auto* value=property(event.pairs,"lefthand"))hands_->value.left=*gesture(*value);
                notifications.push_back({now,current.actor,event.id,event.pairs,current.actor_generation,current.vm_generation,layer});
            }
            changed=true;
        }
        const auto numeric=static_cast<double>(current.frame);
        if(!std::isfinite(numeric)||numeric<std::numeric_limits<std::int32_t>::min()||numeric>std::numeric_limits<std::int32_t>::max())return "animation frame range exceeded";
        const auto integral=std::trunc(numeric);
        if(integral<0||integral>=current.frames){
            if(current.loop)current.frame+=current.backwards?static_cast<float>(current.frames):-static_cast<float>(current.frames);
            else current.end=true;
            changed=true;
        }
        return nullptr;
    };
    if(const auto* why=advance_one(*state_,source_walk_?source_walk_->weights[0]:1.0f,0))return failed(why);
    if(source_walk_)for(std::size_t lane=0;lane<2;++lane)
        if(const auto* why=advance_one(source_walk_->secondary[lane],source_walk_->weights[lane+1],static_cast<std::uint8_t>(lane+1)))return failed(why);
    for(const auto& event:notifications)if(observed)observed(event);
    return report_success||changed;
}
}
