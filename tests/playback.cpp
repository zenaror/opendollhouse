#include "odh/playback.hpp"
#include <iostream>
#include <cmath>

namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::shared_ptr<odh::AnimationHeader> clip(unsigned frames=3){
    auto a=std::make_shared<odh::AnimationHeader>();a->name="clip";
    odh::BoneMotion m{};m.frames=frames;a->motions.push_back(m);return a;
}
odh::Bhav routine(std::uint8_t flags=0,std::uint8_t expected=0,std::uint16_t id=1){
    return {0x8002,{{44,254,1,{static_cast<std::uint8_t>(id),static_cast<std::uint8_t>(id>>8),0,0,0,flags,expected,0}},
                   {2,0,255,{0,0,0,0,0,5,8,9}}},{},odh::Bhav::Metadata{1,2,0,0}};
}
struct Fixture {
    odh::World world;
    std::shared_ptr<odh::AnimationHeader> a=clip(),reset_a=clip(),route_a=clip(7);
    std::uint16_t route_index=0;bool route_swimming=false;
    std::int16_t posture=-1;
    bool carry_resolved=false;
    std::uint32_t source=0;std::uint16_t selected=0;
    odh::Playback player{world,[&](const auto&,auto scope,auto id){source=scope;selected=id;return a;}};
    odh::Vm vm;
    Fixture(std::uint8_t flags=0,std::uint8_t expected=0,std::uint16_t id=1,odh::PlaybackLimits limits={},bool reset_resources=true,
        odh::EntityLayout layout={0,88,1},bool populate_world=true):
        player{world,[&](const auto&,auto scope,auto selected_id){source=scope;selected=selected_id;return a;},limits,reset_resources?odh::AnimationResetResolver{[&](const auto&,auto value){if(value==-1)carry_resolved=true;else posture=value;return reset_a;}}:odh::AnimationResetResolver{},
        [&](const auto&,std::uint16_t index,bool swimming){route_index=index;route_swimming=swimming;return route_a;}},
        vm({{odh::routine_key(4096),routine(flags,expected,id)}},world.vm_memory(),{},player.primitive()){
        if(populate_world){world.create(1,odh::EntityKind::avatar,layout);world.declare_object_word(1,17,0);}
        player.bind(vm);
    }
    void start(const std::vector<std::int16_t>& args={}){vm.start(odh::routine_key(4096),{1,2,2},args);vm.run(20);}
    odh::PrimitiveOutcome apply(std::uint8_t flags,std::uint16_t id=0){
        const odh::VmFrame frame{odh::routine_key(4096),0,{1,2,2},{0,0},{0,0}};
        return player.primitive()(frame,routine(flags,0,id).instructions[0],vm.temps(),{&vm,0});
    }
    void xevt(std::int32_t time,const std::string& value){a->motions[0].time_properties={{{time,{{"xevt",value}}}}};}
};
bool same_primary(const odh::PlaybackSnapshot& a,const odh::PlaybackSnapshot& b){
    if(bool(a.state)!=bool(b.state)||bool(a.hands)!=bool(b.hands)||a.last_tick!=b.last_tick)return false;
    if(a.hands&&(a.hands->actor!=b.hands->actor||a.hands->actor_generation!=b.hands->actor_generation||
        a.hands->vm_generation!=b.hands->vm_generation||a.hands->value.left!=b.hands->value.left||
        a.hands->value.right!=b.hands->value.right))return false;
    if(!a.state)return true;
    const auto& x=*a.state;const auto& y=*b.state;
    if(!(x.resource==y.resource)||x.actor!=y.actor||x.actor_generation!=y.actor_generation||x.vm_generation!=y.vm_generation||
       x.frame!=y.frame||x.speed!=y.speed||x.frames!=y.frames||x.backwards!=y.backwards||x.end!=y.end||x.loop!=y.loop||
       x.low!=y.low||x.high!=y.high||x.queue!=y.queue||x.events_run!=y.events_run||x.ticks!=y.ticks||bool(x.wait)!=bool(y.wait))return false;
    return !x.wait||(x.wait->ticket==y.wait->ticket&&x.wait->bank==y.wait->bank&&
        x.wait->index==y.wait->index&&x.wait->expected==y.wait->expected);
}
void default_slot_carry_tests(){
    Fixture source;
    require(source.apply(16,9).disposition==odh::PrimitiveDisposition::goto_true&&source.player.carry_clip()==source.a,
        "custom carry exists before the new attachment");
    source.a->motions[0].time_properties={{{0,{{"xevt","7"},{"lefthand","2"},{"righthand","1"}}}}};
    source.reset_a->name="a2o-rarm-carry-loop";
    source.reset_a->motions[0].time_properties={{{0,{{"xevt","99"},{"lefthand","3"}}}}};
    source.start();require(source.player.tick(1)&&source.vm.status()==odh::VmStatus::waiting,
        "active main animation retains its wait while timed hands and xevt queue are committed");
    require(source.world.create(2,odh::EntityKind::object)&&source.world.attach(1,0,2),"new reciprocal hand attachment");
    const auto frame=source.vm.stack().back();const auto actor_generation=source.world.find(1)->generation;
    const auto object_generation=source.world.find(2)->generation;const auto run=source.vm.run_generation();
    const auto steps=source.vm.steps(),ticket=source.vm.pending()->ticket;
    odh::PlaybackSnapshot before,after;std::string why;
    require(source.player.capture_snapshot(before,&why)&&before.state&&before.state->wait&&!before.state->queue.empty(),
        "capture animation, pending wait, event queue and gestures before carry attachment");
    require(source.player.attach_default_carry(frame,source.vm,actor_generation,run,2,object_generation,&why),
        "real slot0 attachment installs source default carry without an Anim/reset instruction");
    require(source.player.capture_snapshot(after,&why)&&same_primary(before,after)&&after.carry&&
        after.carry->kind==odh::PlaybackResourceKey::Kind::posture_reset&&after.carry->posture==-1&&
        !after.carry->source&&!after.carry->id&&after.carry->caller==frame.context.caller&&
        after.carry->callee==frame.context.callee&&after.carry->stack_object==frame.context.stack_object&&
        after.carry->code_owner==frame.context.code_owner&&after.carry->actor_age==source.world.find(1)->person_data[58]&&
        source.player.carry_clip()==source.reset_a&&source.vm.steps()==steps&&source.vm.run_generation()==run&&
        source.vm.status()==odh::VmStatus::waiting&&source.vm.pending()->ticket==ticket,
        "new attachment replaces custom carry but preserves exact main/wait/hands/events and VM identity");
    Fixture resumed(0,0,1,{},true,{0,88,1},false);resumed.a=std::make_shared<odh::AnimationHeader>(*source.a);
    resumed.reset_a=std::make_shared<odh::AnimationHeader>(*source.reset_a);
    require(resumed.world.restore_snapshot(source.world.snapshot())&&resumed.vm.restore_snapshot(source.vm.snapshot()),
        "restore World attachment and waiting VM independently");
    require(resumed.player.restore_snapshot(after,resumed.vm,[&](const auto& key)->std::shared_ptr<const odh::AnimationHeader>{
        if(key.kind==odh::PlaybackResourceKey::Kind::posture_reset&&key.posture==-1)return resumed.reset_a;
        if(key.kind==odh::PlaybackResourceKey::Kind::animation&&key.id==1)return resumed.a;
        return {};
    },&why)&&resumed.player.carry_clip()==resumed.reset_a,"default carry resource key restores alongside active animation wait");
    odh::PlaybackSnapshot reopened;
    require(resumed.player.capture_snapshot(reopened,&why)&&same_primary(after,reopened)&&reopened.carry==after.carry,
        "carry snapshot reopen preserves every primary continuation field");
    source.player.tick(2);resumed.player.tick(2);
    require(source.vm.stack().back().arguments==resumed.vm.stack().back().arguments&&
        source.vm.status()==resumed.vm.status()&&source.player.progress()->frame==resumed.player.progress()->frame&&
        source.player.hands()->left==resumed.player.hands()->left,
        "default carry attachment does not consume pending main events or change restored continuation");

    Fixture clearing(0,0,1,{},true,{0,88,3});clearing.start();
    require(clearing.world.create(2,odh::EntityKind::object)&&clearing.world.create(3,odh::EntityKind::object)&&
        clearing.world.attach(1,0,2)&&clearing.world.attach(1,1,3),"attach hand and head occupants");
    const auto clear_frame=clearing.vm.stack().back();const auto clear_actor=clearing.world.find(1)->generation;
    const auto clear_run=clearing.vm.run_generation();
    require(clearing.world.detach(1,0)&&clearing.world.attach(1,0,2)&&
        clearing.player.attach_default_carry(clear_frame,clearing.vm,clear_actor,clear_run,2,clearing.world.find(2)->generation,&why)&&
        clearing.player.capture_snapshot(before,&why),"prepare default carry before source ClearSlot");
    require(!clearing.player.clear_slot_carry(clear_frame,clearing.vm,clear_actor,clear_run,1,&why)&&clearing.player.carry_clip(),
        "carry clear hook rejects an occupied slot before World detaches it");
    require(clearing.world.detach(1,1)&&clearing.player.clear_slot_carry(clear_frame,clearing.vm,clear_actor,clear_run,1,&why)&&
        !clearing.player.carry_clip()&&clearing.world.find(1)->slots[0]==2&&
        clearing.player.capture_snapshot(after,&why)&&same_primary(before,after),
        "source ClearSlot1 clears carry even with slot0 held, without resetting primary animation or wait");
    require(clearing.world.detach(1,0)&&clearing.world.attach(1,0,2)&&
        clearing.player.attach_default_carry(clear_frame,clearing.vm,clear_actor,clear_run,2,clearing.world.find(2)->generation,&why)&&
        clearing.world.detach(1,0)&&clearing.player.clear_slot_carry(clear_frame,clearing.vm,clear_actor,clear_run,0,&why)&&
        !clearing.player.carry_clip(),"source hand-slot clear drops carry too");

    Fixture rejected;rejected.start();
    require(rejected.world.create(2,odh::EntityKind::object)&&rejected.world.attach(1,0,2),"rejection fixture hand attachment");
    const auto source_frame=rejected.vm.stack().back();const auto generation=rejected.world.find(1)->generation;
    const auto vm_generation=rejected.vm.run_generation(),held_generation=rejected.world.find(2)->generation;
    require(!rejected.player.attach_default_carry(source_frame,rejected.vm,generation+1,vm_generation,2,held_generation,&why)&&
        !rejected.player.attach_default_carry(source_frame,rejected.vm,generation,vm_generation+1,2,held_generation,&why)&&
        !rejected.player.attach_default_carry(source_frame,rejected.vm,generation,vm_generation,2,held_generation+1,&why)&&
        !rejected.player.carry_clip()&&!rejected.carry_resolved,"stale actor, VM or held-object identity rejects before resource resolution");
    auto wrong_frame=source_frame;wrong_frame.context.callee=17;
    Fixture wrong_vm;wrong_vm.start();
    require(!rejected.player.attach_default_carry(wrong_frame,rejected.vm,generation,vm_generation,2,held_generation,&why)&&
        !rejected.player.attach_default_carry(source_frame,wrong_vm.vm,generation,vm_generation,2,held_generation,&why),
        "captured source frame and bound source VM cannot cross execution graphs");
    require(rejected.player.capture_snapshot(before,&why),"capture before missing carry resource");
    rejected.reset_a.reset();
    require(!rejected.player.attach_default_carry(source_frame,rejected.vm,generation,vm_generation,2,held_generation,&why)&&
        rejected.player.capture_snapshot(after,&why)&&same_primary(before,after)&&!after.carry&&
        rejected.vm.pending()&&rejected.vm.status()==odh::VmStatus::waiting,
        "missing default carry is explicit and preserves existing main wait/state");
    require(rejected.world.detach(1,0)&&!rejected.player.attach_default_carry(source_frame,rejected.vm,generation,vm_generation,2,held_generation,&why),
        "detached object cannot install default carry without reciprocal attachment");
}
// Create42 executes synchronously. The carry hook must authenticate that
// active dispatch rather than requiring the creator to have already returned.
void synchronous_created_carry_tests(){
    for(const bool other_carrier:{false,true})for(unsigned mutation=0;mutation<5;++mutation){
        if(mutation==4&&!other_carrier)continue; // update_pending is legal only on another idle VM.
        odh::World world;
        require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&
            world.create(2,odh::EntityKind::avatar,{0,88,1})&&world.create(3,odh::EntityKind::object),
            "Create carry fixture has two independent avatar identities and a real object");
        require(world.declare_object_word(1,17,0)&&world.declare_object_word(2,17,0),
            "carrier animation speed has explicit source fields");
        const auto carrier=static_cast<std::int16_t>(other_carrier?2:1);
        const auto actor_generation=world.find(carrier)->generation,held_generation=world.find(3)->generation;
        auto carry=clip(),primary=clip(30);unsigned resolves=0;bool nested_rejected=false;
        primary->motions[0].time_properties={{{0,{{"xevt","7"},{"lefthand","2"},{"righthand","1"}}}}};
        odh::Playback* playback=nullptr;odh::Vm* carrier_vm=nullptr;
        odh::VmFrame active_frame;odh::Instruction active_instruction{};odh::VmDispatch active_dispatch{};
        odh::Playback player(world,[&](const auto&,auto,auto){return primary;},{},
            [&](const auto&,auto posture){
                ++resolves;require(posture==-1,"creation uses default carry posture only");
                std::string nested;
                nested_rejected=!playback->attach_default_carry(carrier_vm->stack().back(),*carrier_vm,
                    actor_generation,carrier_vm->run_generation(),3,held_generation,&nested,
                    &active_frame,&active_instruction,&active_dispatch);
                if(mutation==1)require(world.detach(carrier,0),"resolver mutation detaches exact held object");
                if(mutation==2)require(world.set_person_data(carrier,58,1),"resolver mutation changes carrier age");
                if(mutation==3)require(world.remove(1),"resolver mutation retires the actual creation source");
                if(mutation==4)require(carrier_vm->pending()&&carrier_vm->update_pending(carrier_vm->pending()->ticket,
                    {odh::VmFrameBank::arguments,0,73}),"real resolver updates another carrier's pending argument");
                return carry;
            });
        playback=&player;
        odh::Vm idle({{odh::routine_key(4096),routine()}},world.vm_memory(),{},player.primitive());
        const odh::Instruction create{42,254,255,{}};
        bool accepted=false;std::string why;
        odh::Vm creator({{odh::routine_key(4096),{0x8002,{create},{},odh::Bhav::Metadata{0,0,0,0}}}},
            world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                active_frame=frame;active_instruction=instruction;active_dispatch=dispatch;
                require(world.attach(carrier,0,3),"source slot transfer precedes creation carry presentation");
                const auto captured=carrier_vm->stack().back();const auto run=carrier_vm->run_generation();
                const auto steps=carrier_vm->steps(),depth=carrier_vm->stack().size();
                const auto before=player.carry_clip();
                odh::PlaybackSnapshot primary_before,primary_after;
                if(other_carrier&&!mutation)require(player.capture_snapshot(primary_before,&why)&&primary_before.state&&
                    primary_before.state->wait&&!primary_before.state->queue.empty()&&primary_before.hands,
                    "other carrier starts with an actual animation wait, queued events and gestures");
                // Creator authority cannot be substituted for another carrier's
                // VM, and no active dispatch is accepted by the legacy hook.
                if(!other_carrier)require(!player.attach_default_carry(captured,*carrier_vm,actor_generation,run,
                    3,held_generation,&why)&&resolves==0,"active self creation still rejects unauthenticated safe-point hook");
                auto forged=dispatch;++forged.ticket;
                require(!player.attach_default_carry(captured,*carrier_vm,actor_generation,run,3,held_generation,&why,
                    &frame,&instruction,&forged)&&resolves==0,"forged or foreign Create dispatch cannot grant carry authority");
                auto wrong=instruction;wrong.opcode=43;
                require(!player.attach_default_carry(captured,*carrier_vm,actor_generation,run,3,held_generation,&why,
                    &frame,&wrong,&dispatch)&&resolves==0,"an unrelated active slot primitive cannot impersonate Create42");
                require(!player.attach_default_carry(captured,*carrier_vm,actor_generation,run+1,3,held_generation,&why,
                    &frame,&instruction,&dispatch)&&resolves==0,
                    "stale carrier run refuses carry before resolving resources");
                accepted=player.attach_default_carry(captured,*carrier_vm,actor_generation,run,3,held_generation,&why,
                    &frame,&instruction,&dispatch);
                require(carrier_vm->run_generation()==run&&carrier_vm->steps()==steps&&carrier_vm->stack().size()==depth&&
                    (mutation==4?carrier_vm->stack().back().arguments.at(0)==73:
                        odh::vm_frame_equal(captured,carrier_vm->stack().back())),
                    "carry neither resets nor advances frames and never rolls back an external pending write");
                require(nested_rejected&&resolves==1,"resource lookup cannot reenter carry publication");
                if(mutation)require(!accepted&&player.carry_clip()==before,"resolver identity mutation refuses publication without replacing carry");
                else require(accepted&&player.carry_clip()==carry,"exact self-active or other-idle carrier binds original default carry");
                if(other_carrier&&!mutation)require(player.capture_snapshot(primary_after,&why)&&
                    same_primary(primary_before,primary_after)&&primary_after.carry,
                    "successful creation preserves carrier wait, queued events, cursors and gestures exactly");
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });
        carrier_vm=other_carrier?&idle:&creator;
        require(player.bind(*carrier_vm),"playback is bound to actual carrier VM");
        if(other_carrier){
            require(idle.start(odh::routine_key(4096),{2,2,2,0})==odh::VmStatus::running,"start independent carrier continuation");
            idle.run(1);require(idle.status()==odh::VmStatus::waiting&&player.tick(1),
                "other carrier retains original animation wait with real queued animation events");
        }
        require(creator.start(odh::routine_key(4096),{1,1,1,0})==odh::VmStatus::running,"start actual Create42 dispatch");
        creator.run(1);
        require(creator.status()==odh::VmStatus::returned_true,"Create fixture executes authenticated carry callback once");
        if(other_carrier)require(idle.status()==odh::VmStatus::waiting&&idle.pending(),"other carrier pending wait survives creation");
        if(!other_carrier&&!mutation){
            require(!player.attach_default_carry(active_frame,creator,actor_generation,creator.run_generation(),
                3,held_generation,&why,&active_frame,&active_instruction,&active_dispatch),"expired Create dispatch cannot be replayed after primitive return");
        }
    }
}
void prepared_carry_transfer_tests(){
    struct Transfer {
        odh::World world;
        std::shared_ptr<odh::AnimationHeader> primary=clip(40),carry=clip(3),reset=clip(3);
        odh::Playback source,destination;odh::Vm source_vm,destination_vm;
        std::int16_t target=105;std::size_t slot=0;
        bool missing=false,throws=false,stale=false,wrong_run=false,mutate=false,success=false;
        unsigned resolves=0;std::int16_t resolver_caller=0;
        explicit Transfer(std::uint16_t opcode=43):
            source(world,[this](const auto&,auto,auto id){return id==2?carry:primary;},{},
                [this](const auto& frame,auto){++resolves;resolver_caller=frame.context.caller;
                    if(throws)throw std::runtime_error("missing default");return missing?nullptr:reset;}),
            destination(world,[this](const auto&,auto,auto id){return id==2?carry:primary;},{},
                [this](const auto& frame,auto){++resolves;resolver_caller=frame.context.caller;
                    if(throws)throw std::runtime_error("missing default");
                    if(mutate)world.set_person_data(105,58,99);
                    return missing?nullptr:reset;}),
            source_vm({{odh::routine_key(4096),{0x8002,{{opcode,0,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}},
                world.vm_memory(),{},[this](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                    auto move=world.prepare_move_to_slot(target,slot,200);
                    if(!move)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"move preparation failed"};
                    if(move->already_in_slot()){success=true;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};}
                    std::vector<odh::Playback::PreparedCarryChange> changes;
                    const auto prepare=[&](std::int16_t carrier,odh::Playback& playback,odh::Vm& vm){
                        if(instruction.opcode==4){
                            require(frame.context.caller==target&&frame.context.stack_object==200,"real Grab names Caller hand and StackObject item");
                            auto fabricated=instruction;fabricated.opcode=43;const auto before=resolves;
                            require(!playback.prepare_slot_carry_change(*move,frame,source_vm,fabricated,dispatch,carrier,
                                world.find(carrier)->generation,vm.run_generation())&&resolves==before,
                                "real Grab dispatch cannot authenticate a substituted DropOnto opcode");
                        }
                        auto prepared=playback.prepare_slot_carry_change(*move,frame,source_vm,instruction,dispatch,carrier,
                            world.find(carrier)->generation+(stale?1:0),vm.run_generation()+(wrong_run?1:0));
                        if(!prepared)return false;changes.push_back(std::move(*prepared));return true;
                    };
                    const auto old=move->source_attachment();
                    if(old&&!prepare(old->container,old->container==181?source:destination,
                        old->container==181?source_vm:destination_vm))
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"source carry preparation failed"};
                    if(target!= (old?old->container:0)&&slot==0&&!prepare(target,target==181?source:destination,
                        target==181?source_vm:destination_vm))
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"destination carry preparation failed"};
                    for(const auto& change:changes)if(!change.can_commit())
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"carry became stale"};
                    if(!world.commit_move_to_slot(*move))
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"move became stale"};
                    for(const auto& change:changes)require(change.can_publish(),"post-move carry relation prevalidates all carriers");
                    for(auto& change:changes)require(change.publish()&&!change.publish(),"prepared carry publishes once without callbacks");
                    success=true;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
                }),
            destination_vm({{odh::routine_key(4096),routine()}},world.vm_memory(),{},destination.primitive()){
            require(world.create(181,odh::EntityKind::avatar,{0,88,3})&&world.create(105,odh::EntityKind::avatar,{0,88,3})&&
                world.create(200,odh::EntityKind::object)&&world.create(201,odh::EntityKind::object)&&
                world.declare_object_word(200,1,0)&&world.declare_object_word(201,1,0)&&
                world.attach(181,1,200)&&world.attach(105,1,201),"prepared carry creates exact source and destination graph");
            require(source.bind(source_vm)&&destination.bind(destination_vm),"prepared carry binds each actor's own VM");
            primary->motions[0].time_properties={{{0,{{"lefthand","2"},{"xevt","7"}}}}};
            source_vm.start(odh::routine_key(4096),{181,181,opcode==4?std::int16_t{200}:std::int16_t{105}},{0,0,0,0});
            const auto frame=source_vm.stack().back();
            require(source.primitive()(frame,{44,254,255,{1,0,0,0,0,1,0,0}}, {},{&source_vm,0}).disposition==
                odh::PrimitiveDisposition::goto_true&&source.advance(0),"source primary hands and events are live before transfer");
            require(source.primitive()(frame,{44,254,255,{2,0,0,0,0,16,0,0}}, {},{&source_vm,0}).disposition==
                odh::PrimitiveDisposition::goto_true,"source retains custom carry despite moving a nonhand slot");
            destination_vm.start(odh::routine_key(4096),{105,105,105});
            require(destination_vm.run(1)==odh::VmStatus::waiting&&destination.advance(0),
                "destination has independent native44 wait, queue and gestures");
        }
        odh::PlaybackSnapshot saved(odh::Playback& playback){odh::PlaybackSnapshot result;
            require(playback.capture_snapshot(result),"capture prepared carry primary state");return result;}
        void transfer(std::int16_t container,std::size_t new_slot){target=container;slot=new_slot;success=false;
            source_vm.run(1);require(success,"prepared move and carry publish from actual source dispatch");}
    };
    Transfer f;const auto source_before=f.saved(f.source),destination_before=f.saved(f.destination);
    const auto ticket=f.destination_vm.pending()->ticket;
    f.transfer(105,0);
    require(!f.source.carry_clip()&&f.destination.carry_clip()==f.reset&&f.resolver_caller==105&&f.resolves==1&&
        f.world.find(181)->slots[1]==0&&f.world.find(105)->slots[0]==200&&
        same_primary(source_before,f.saved(f.source))&&same_primary(destination_before,f.saved(f.destination))&&
        f.destination_vm.pending()->ticket==ticket,"foreign source43 transfers carry using destination's own frame and preserves both primary graphs");
    const auto destination_carry=f.saved(f.destination);
    require(destination_carry.carry&&destination_carry.carry->caller==105&&destination_carry.carry->callee==105&&
        destination_carry.carry->stack_object==105&&destination_carry.carry->posture==-1,
        "default carry snapshot provenance belongs to destination actor's real bound frame");
    f.transfer(181,2);require(!f.destination.carry_clip()&&!f.source.carry_clip(),"clearing any source avatar slot clears carry without resetting nonhand destination");
    f.transfer(181,0);require(f.source.carry_clip()==f.reset&&f.resolver_caller==181&&f.resolves==2,
        "same-avatar nonhand to hand combines clear then new default in one plan");
    f.transfer(181,1);require(!f.source.carry_clip()&&f.resolves==2,"same-avatar hand to nonhand clears carry only");
    const auto before_noop=f.saved(f.source);f.transfer(181,1);
    require(same_primary(before_noop,f.saved(f.source))&&!f.source.carry_clip()&&f.resolves==2,
        "already-in-slot move skips all carry preparation");
    for(unsigned failure=0;failure<5;++failure){
        Transfer bad;bad.missing=failure==0;bad.throws=failure==1;bad.stale=failure==2;bad.mutate=failure==3;bad.wrong_run=failure==4;
        const auto before=bad.saved(bad.source),other=bad.saved(bad.destination);
        bad.source_vm.run(1);
        require(!bad.success&&bad.world.find(181)->slots[1]==200&&bad.world.find(105)->slots[0]==0&&
            bad.world.find(200)->attachment&&bad.world.find(200)->attachment->container==181&&
            bad.source.carry_clip()==bad.carry&&!bad.destination.carry_clip()&&
            same_primary(before,bad.saved(bad.source)),"failed resource, callback or identity preparation leaves slot graph/carry/primary unpublished");
        if(failure!=3)require(same_primary(other,bad.saved(bad.destination)),"unresolved or throwing carry resolver preserves destination44 controller");
    }
    // The safe-point API must refuse fabricated source proof before resolving.
    Transfer grab(4);grab.transfer(181,0);
    require(grab.source.carry_clip()==grab.reset&&grab.world.find(181)->slots[0]==200&&grab.world.find(181)->slots[1]==0&&
        grab.world.find(200)->attachment&&grab.world.find(200)->attachment->container==181,
        "authentic opcode4 Grab publishes its actual caller hand transfer and default carry");
    for(const bool bad_generation:{false,true}){
        Transfer bad_grab(4);bad_grab.target=181;bad_grab.stale=bad_generation;bad_grab.wrong_run=!bad_generation;
        bad_grab.source_vm.run(1);
        require(!bad_grab.success&&bad_grab.world.find(181)->slots[1]==200&&bad_grab.world.find(181)->slots[0]==0&&
            bad_grab.source.carry_clip()==bad_grab.carry,"Grab rejects wrong carrier generation/run before physical or carry publication");
    }
    Transfer outside;auto move=outside.world.prepare_move_to_slot(105,0,200);
    require(move&&!outside.destination.prepare_slot_carry_change(*move,outside.source_vm.stack().back(),outside.source_vm,
        {43,0,255,{}},{&outside.source_vm,1},105,outside.world.find(105)->generation,outside.destination_vm.run_generation())&&
        outside.resolves==0,"carry preparation refuses an inactive fabricated dispatch");
    // Default carry survives a fresh graph snapshot restore with its own run.
    // Capture a new cross-avatar hand installation, with the original wait still active.
    f.transfer(105,0);const auto carry_snapshot=f.saved(f.destination);
    odh::World carry_world;require(carry_world.restore_snapshot(f.world.snapshot()),"fresh carry snapshot World");
    odh::Playback carry_player(carry_world,{});odh::Vm carry_vm({{odh::routine_key(4096),routine()}},carry_world.vm_memory(),{},carry_player.primitive());
    require(carry_vm.restore_snapshot(f.destination_vm.snapshot())&&carry_player.restore_snapshot(carry_snapshot,carry_vm,
        [&](const auto& key)->std::shared_ptr<const odh::AnimationHeader>{return key.kind==odh::PlaybackResourceKey::Kind::posture_reset?f.reset:f.primary;}),
        "prepared destination default key restores with exact destination generation/run and original primary44 wait");
}
void route_animation_tests(){
    for(bool swimming:{false,true})for(std::uint16_t index=3;index<=9;++index)for(float speed:{1.0f,2.0f}){
        if(index==3&&speed==2)continue;
        Fixture f;f.vm.start(odh::routine_key(4096),{1,2,2},{0,0});
        f.a->motions[0].time_properties={{{0,{{"lefthand","2"},{"xevt","7"}}}}};
        require(f.apply(1,1).disposition==odh::PrimitiveDisposition::goto_true&&f.player.advance(1),
            "route replaces an existing shared primary after its hand/event update");
        require(f.apply(16,2).disposition==odh::PrimitiveDisposition::goto_true,"route fixture custom carry");
        const auto frame=f.vm.stack().back();const auto generation=f.world.find(1)->generation;
        const auto run=f.vm.run_generation();const auto steps=f.vm.steps();std::string why;
        f.route_a->motions[0].time_properties={{{0,{{"righthand","1"},{"xevt","9"}}}}};
        require(f.player.start_route_animation(frame,f.vm,generation,run,index,swimming,speed,index==3,&why),
            "explicit walking/swimming route table index resolves at a safe point");
        require(f.route_index==index&&f.route_swimming==swimming&&f.vm.steps()==steps&&!f.vm.pending(),
            "route service does not dispatch VM or synthesize opcode44 wait");
        odh::PlaybackSnapshot saved;
        require(f.player.capture_snapshot(saved,&why)&&saved.state->resource.kind==odh::PlaybackResourceKey::Kind::route_walk&&
            saved.state->resource.source==unsigned(swimming)&&saved.state->resource.id==index&&saved.state->speed==speed&&
            saved.state->frame==0&&saved.state->queue.empty()&&!saved.state->wait&&
            saved.hands->value.left==odh::HandGesture::fist&&f.player.carry_clip()==f.a,
            "route replacement discards prior primary events and preserves hands/carry");
        require(f.player.advance(2)&&f.player.sample()->frame==speed&&
            f.player.hands()->right==odh::HandGesture::pointing&&f.player.progress()->queued_events==1,
            "owning Playback advances route at exact source speed and applies timed properties");
        require(!f.player.advance(2)&&f.player.capture_snapshot(saved,&why),"route cannot advance twice per tick");
        Fixture resumed(0,0,1,{},true,{0,88,1},false);
        resumed.route_a=std::make_shared<odh::AnimationHeader>(*f.route_a);
        require(resumed.world.restore_snapshot(f.world.snapshot())&&resumed.vm.restore_snapshot(f.vm.snapshot()),
            "route restore uses a fresh execution graph");
        const auto resolver=[&](const odh::PlaybackResourceKey& key)->std::shared_ptr<const odh::AnimationHeader>{
            if(key.kind==odh::PlaybackResourceKey::Kind::route_walk&&key.source==unsigned(swimming)&&key.id==index)
                return resumed.route_a;
            return key.kind==odh::PlaybackResourceKey::Kind::animation?resumed.a:nullptr;
        };
        require(resumed.player.restore_snapshot(saved,resumed.vm,resolver,&why),"route snapshot resolves by explicit route key");
        odh::PlaybackSnapshot restored;
        require(resumed.player.capture_snapshot(restored,&why)&&same_primary(saved,restored)&&
            resumed.player.sample()->clip.get()!=f.player.sample()->clip.get(),"fresh route graph preserves exact cursors and identity");
        for(std::uint64_t tick=3;tick<=9;++tick){
            require(f.player.advance(tick)&&resumed.player.advance(tick),"continuous and restored route advance");
            require(f.player.capture_snapshot(saved,&why)&&resumed.player.capture_snapshot(restored,&why)&&same_primary(saved,restored),
                "restored route retains exact frame/end/event/hand state without replay");
        }
        require(f.player.sample()->end==(index!=3)&&f.player.sample()->loop==(index==3),
            "source turn ends at clip frames while walk/swim reset loops");
        for(int mutation=0;mutation<8;++mutation){
            auto bad=saved;
            if(mutation==0)bad.state->resource.source=2;
            if(mutation==1)bad.state->resource.id=10;
            if(mutation==2)bad.state->resource.posture=1;
            if(mutation==3)bad.state->speed=1.2f;
            if(mutation==4)bad.state->backwards=true;
            if(mutation==5)bad.state->loop=!bad.state->loop;
            if(mutation==6)bad.state->wait=odh::PlaybackWaitSnapshot{1};
            if(mutation==7)++bad.state->actor_generation;
            require(!resumed.player.restore_snapshot(bad,resumed.vm,resolver,&why),"malformed or stale route snapshot rejects");
        }
        auto wrong=frame;++wrong.context.callee;
        require(!f.player.start_route_animation(wrong,f.vm,generation,run,4,false,1,false,&why)&&
            !f.player.start_route_animation(frame,f.vm,generation+1,run,4,false,1,false,&why)&&
            !f.player.start_route_animation(frame,f.vm,generation,run+1,4,false,1,false,&why),
            "stale route context, actor and run reject");
        f.route_a.reset();
        require(!f.player.start_route_animation(frame,f.vm,generation,run,4,false,1,false,&why)&&
            f.player.capture_snapshot(restored,&why)&&same_primary(saved,restored),"missing route resource preserves current primary");
    }
    Fixture waiting;waiting.start();const auto frame=waiting.vm.stack().back();
    require(!waiting.player.start_route_animation(frame,waiting.vm,waiting.world.find(1)->generation,
        waiting.vm.run_generation(),4,false,1,false),"route service cannot erase a native opcode44 wait");
}
odh::Bhav immediate_animation(std::uint8_t flags,std::uint16_t id) {
    auto result=routine(flags,0,id);result.instructions.resize(1);
    result.instructions[0].on_false=255;return result;
}
struct ImmediateFixture {
    odh::World world;
    std::shared_ptr<odh::AnimationHeader> a=clip(40),b=clip(60),carry=clip(5),reset=clip(4);
    odh::Playback player;
    odh::Vm vm;
    explicit ImmediateFixture(bool initialize=true)
        :player(world,[this](const auto&,auto,auto id)->std::shared_ptr<const odh::AnimationHeader>{
            return id==1?a:id==2?b:id==3?carry:nullptr;
        },{},[this](const auto&,auto){return reset;}),
         vm({{odh::routine_key(4096),routine()}},world.vm_memory(),{},
            [this](const auto& f,const auto& i,const auto& t,const auto& d)->odh::PrimitiveOutcome{
                if(i.opcode==42)return {odh::PrimitiveDisposition::wait,{}};
                return player.primitive()(f,i,t,d);
            },{{22,{
                {odh::routine_key(4097),{0x8002,{{42,1,255,{}},
                    {2,255,255,{1,0,9,0,0,5,8,7}}},{},odh::Bhav::Metadata{1,2,0,0}}},
                {odh::routine_key(4098),routine()},
                {odh::routine_key(4099),immediate_animation(1,2)},
                {odh::routine_key(4100),immediate_animation(0,0)},
                {odh::routine_key(4101),immediate_animation(16,3)},
                {odh::routine_key(4102),{0x8002,{{44,1,255,{3,0,0,0,0,16,0,0}},
                    {44,254,255,{0,0,0,0,0,17,0,0}}},{},odh::Bhav::Metadata{1,2,0,0}}}
            }}}) {
        a->name="primary-a";b->name="primary-b";carry->name="carry";reset->name="posture";
        a->motions[0].time_properties={{{0,{{"xevt","7"},{"lefthand","2"}}}}};
        if(initialize){
            require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&world.create(2,odh::EntityKind::object)&&
                world.create(3,odh::EntityKind::object)&&world.declare_object_word(1,17,0),"immediate playback World");
        }
        require(player.bind(vm),"immediate playback binds its sole VM");
    }
    void start() {
        vm.start(odh::routine_key(4096),{1,2,2,0});
        require(vm.run(1)==odh::VmStatus::waiting,"parent AnimateSim owns native wait");
    }
    std::uint64_t inject(unsigned child) {
        const auto parent=vm.pending()->ticket;
        const auto ticket=vm.inject_immediate({odh::routine_key(static_cast<std::uint16_t>(child)),{1,3,3,22},{},
            odh::VmCallReturnMode::ordinary},[this](const auto& pending,const auto& frame){
                return player.can_park_for_immediate(pending,frame);
            });
        const auto description="immediate child "+std::to_string(child);
        require(ticket.has_value(),(description+" injects a valid source frame").c_str());
        require(player.park_for_immediate(parent),(description+" parks only old animation wait").c_str());
        return *ticket;
    }
    odh::PlaybackSnapshot state() {
        odh::PlaybackSnapshot result;std::string why;
        require(player.capture_snapshot(result,&why),"capture immediate/shared animation continuation");return result;
    }
    void ack(std::uint64_t ticket,std::uint64_t parent) {
        require(vm.resume_immediate(ticket)&&player.retry_immediate_parent(parent),
            "ACK restores parent44 and retry leaves original PC unbranched");
    }
};
void immediate_preemption_tests() {
    { // While async Create42 runs, the avatar still animates and queues events.
        ImmediateFixture source;source.start();const auto parent=source.vm.pending()->ticket;
        const auto original=source.state();const auto frame=source.vm.stack().back();
        auto bad=*source.vm.pending();++bad.ticket;
        require(!source.player.can_park_for_immediate(bad,frame)&&
            !source.player.park_for_immediate(parent)&&!source.player.retry_immediate_parent(parent),
            "wrong ticket and out-of-phase park/retry reject without changing parent");
        const auto immediate=source.inject(4097);
        const auto parked=source.state();
        require(!parked.state->wait&&parked.state->frame==original.state->frame&&
            parked.state->low==original.state->low&&parked.state->queue==original.state->queue&&
            source.vm.stack()[0].pc==0&&!source.vm.complete(parent,true),
            "parking preserves shared primary and frame, without completing parent");
        require(source.vm.run(1)==odh::VmStatus::waiting&&source.vm.pending()->instruction.instruction.opcode==42&&
            source.player.advance(1)&&source.vm.pending()->instruction.instruction.opcode==42&&
            source.player.sample()&&source.player.hands()->left==odh::HandGesture::fist&&
            source.player.progress()->queued_events==1,"shared animation advances and changes hands while child42 waits");
        const auto saved=source.state();const auto saved_vm=source.vm.snapshot();
        ImmediateFixture restored(false);restored.a=std::make_shared<odh::AnimationHeader>(*source.a);
        require(restored.world.restore_snapshot(source.world.snapshot())&&restored.vm.restore_snapshot(saved_vm),
            "restore fresh World and immediate child42 VM graph");
        const auto& frozen=*restored.vm.immediate_continuations().back().parent_pending;
        require(restored.player.validates_immediate_parent(frozen,restored.vm.stack()[0]),
            "restore audit proves parked44 before playback state is restored");
        std::string why;
        require(restored.player.restore_snapshot(saved,restored.vm,[&](const auto& key)->std::shared_ptr<const odh::AnimationHeader>{
            return key.id==1?restored.a:nullptr;
        },&why),"restore shared animated state independently of parked parent wait");
        for(auto* f:{&source,&restored}){
            require(f->player.service_wait()&&f->player.advance(2)&&f->vm.stack()[0].arguments[0]==0&&
                f->player.progress()->queued_events==1,"source phases never consume parked parent event queue");
            const auto child=f->vm.pending()->ticket;
            require(f->vm.complete(child,true)&&f->vm.run(1)==odh::VmStatus::yielded&&
                f->vm.immediate_return()->ticket==immediate,"real child42 completion returns through immediate channel");
            f->ack(immediate,parent);
            require(f->vm.stack()[0].pc==0&&f->vm.stack()[0].arguments[0]==0&&
                f->vm.run(1)==odh::VmStatus::budget_exhausted&&f->vm.stack()[0].pc==1&&
                f->vm.stack()[0].arguments[0]==7&&f->vm.temps()[1]==9,
                "real parent44 redispatch consumes shared event, preserving child TEMP and original false branch");
        }
        require(same_primary(source.state(),restored.state()),"fresh graph reproduces shared state after parent reentry");
    }
    { // Source same-clip reuse retains cursors but changes request provenance.
        ImmediateFixture f;f.start();require(f.player.advance(1),"queue first primary event before child slot");
        const auto parent=f.vm.pending()->ticket;const auto before=f.state();const auto immediate=f.inject(4098);
        require(f.vm.run(2)==odh::VmStatus::budget_exhausted&&f.vm.run(1)==odh::VmStatus::waiting,
            "child consumes shared event and waits again on same clip from owner22/callee3");
        const auto reused=f.state();
        require(reused.state->resource.code_owner==22&&reused.state->resource.callee==3&&
            reused.state->frame==before.state->frame&&reused.state->low==before.state->low&&
            f.player.hands()->left==odh::HandGesture::fist&&reused.state->wait,
            "same immutable clip rebinds provenance without restarting frames, events or hands");
        ImmediateFixture resumed(false);
        require(resumed.world.restore_snapshot(f.world.snapshot())&&resumed.vm.restore_snapshot(f.vm.snapshot())&&
            resumed.player.restore_snapshot(reused,resumed.vm,[&](const auto&)->std::shared_ptr<const odh::AnimationHeader>{return resumed.a;}),
            "same-clip child wait restores despite parent's different frozen context");
        // Complete through actual polling/end handling, without synthesizing an Anim result.
        for(std::uint64_t tick=2;tick<50&&!f.vm.immediate_return();++tick){
            require(f.player.service_wait(),"release child44 for actual budgeted polling");
            f.vm.run(2);require(f.player.advance(tick),"advance shared clip after child VM work");
        }
        require(f.vm.immediate_return()&&!f.player.progress(),"child consumes shared end and clears current primary");
        f.ack(immediate,parent);
        require(f.vm.run(1)==odh::VmStatus::waiting&&f.player.progress()->frame==0&&f.state().state->resource.callee==2,
            "parent44 restarts its requested clip because child consumed current shared animation");
    }
    for(const unsigned child:{4099U,4100U,4101U,4102U}){
        ImmediateFixture f;f.start();f.player.advance(1);const auto before=f.state();
        const auto parent=f.vm.pending()->ticket;const auto immediate=f.inject(child);
        if(child==4102)require(f.vm.run(1)==odh::VmStatus::budget_exhausted&&f.player.carry_clip()==f.carry,
            "carry-clear child first establishes real custom carry state");
        require(f.vm.run(1)==odh::VmStatus::yielded,"replacement/reset/carry child executes its real opcode44");
        const auto changed=f.state();
        if(child==4099)require(changed.state&&changed.state->resource.id==2&&changed.state->frame==0&&
            changed.state->loop&&changed.state->queue.empty(),"child mode1 replaces shared primary and discards its old queue");
        if(child==4100)require(changed.state&&changed.state->resource.kind==odh::PlaybackResourceKey::Kind::posture_reset&&
            changed.state->loop&&changed.state->queue.empty(),"child reset replaces shared primary with posture loop");
        if(child==4101)require(changed.carry&&changed.carry->id==3&&changed.state->frame==before.state->frame&&
            changed.state->queue==before.state->queue,"child mode2 updates carry while primary events/frame continue");
        if(child==4102)require(!changed.carry&&changed.state->frame==before.state->frame&&
            changed.state->queue==before.state->queue,"child mode3 zero clears carry without restoring/resetting primary");
        const auto& pending=*f.vm.immediate_continuations().back().parent_pending;
        require(f.player.validates_immediate_parent(pending,f.vm.stack()[0]),
            "parked parent proof is independent of current replacement/reset/carry animation");
        f.ack(immediate,parent);f.vm.run(1);
        if(child==4099||child==4100)require(f.vm.status()==odh::VmStatus::waiting&&f.player.progress()->frame==0&&
            f.state().state->resource.id==1,"parent real reexecution selects its own clip after child replacement");
        else require(f.vm.stack()[0].pc==1&&f.vm.stack()[0].arguments[0]==7,
            "carry-only child leaves shared event for parent real reexecution");
    }
    { // Stale identities and proof arguments are never an invitation to cancel.
        ImmediateFixture f;f.start();const auto saved=f.state();const auto pending=*f.vm.pending();
        auto wrong=f.vm.stack().back();++wrong.context.callee;
        require(!f.player.can_park_for_immediate(pending,wrong)&&!f.player.validates_immediate_parent(pending,wrong),
            "wrong source frame cannot audit animation preemption");
        f.world.remove(1);f.world.create(1,odh::EntityKind::avatar,{0,88,1});
        require(!f.player.can_park_for_immediate(pending,f.vm.stack().back())&&
            !f.player.service_wait()&&f.vm.pending()->ticket==pending.ticket,
            "recycled actor fails audit/service without forcing animation completion");
        (void)saved;
    }
    {
        ImmediateFixture f;f.start();const auto pending=*f.vm.pending();const auto frame=f.vm.stack().back();
        f.vm.start(odh::routine_key(4096),{1,2,2,0});
        require(!f.player.can_park_for_immediate(pending,frame)&&!f.player.park_for_immediate(pending.ticket)&&
            !f.player.retry_immediate_parent(pending.ticket)&&f.vm.status()==odh::VmStatus::running&&f.vm.steps()==0,
            "old animation proof and tickets cannot cross VM run generation");
    }
    {ImmediateFixture idle;require(idle.player.service_wait()&&idle.player.advance(1)&&!idle.player.advance(1),
        "source phase success distinguishes valid idle from invalid duplicate clock");}
}

}
void source_reset_preserves_carry_and_hands(){
    Fixture f;
    require(f.apply(16,9).disposition==odh::PrimitiveDisposition::goto_true,"source reset carry setup");
    const auto carry=f.player.carry_clip();
    f.a->motions[0].time_properties={{{0,{{"lefthand","2"},{"righthand","1"}}}}};
    f.start();require(f.player.tick(1)&&f.vm.pending(),"source reset owns actual animation wait");
    const auto ticket=f.vm.pending()->ticket;const auto hands=f.player.hands();
    require(hands&&f.player.discard_for_source_reset()&&f.vm.pending()&&f.vm.pending()->ticket==ticket,
        "discard retires animation service without completing its old VM ticket");
    require(f.vm.start_after_reset(odh::routine_key(4096),{1,1,0,0})==odh::VmStatus::running&&f.player.finish_source_reset(),
        "actual Reset starts new Main before rebase of retained visual state");
    require(f.player.carry_clip()==carry&&f.player.hands()&&f.player.hands()->left==hands->left&&f.player.hands()->right==hands->right&&!f.player.progress(),
        "source Reset clears primary animation but preserves same carry clip and hand gestures");
}
void ordinary_animation_absence_tests(){
    using Availability=odh::AnimationAvailability;
    for(const auto flags:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{16},std::uint8_t{17}}){
        odh::World world;require(world.create(1,odh::EntityKind::avatar,{0,88,1}),"create absence scheduling actor");
        unsigned lookups=0;odh::Playback player(world,{});
        player.set_animation_lookup_resolver([&](const auto&,std::uint32_t source,std::uint16_t id){
            ++lookups;require(source==0&&id==9,"typed lookup sees exact normalized request");
            return odh::AnimationLookup{Availability::absent,{}, {}};
        });
        auto program=routine(flags,4,9);program.instructions[0].on_true=1;
        program.instructions[1]={2,254,255,{1,0,77,0,0,5,8,7}};
        odh::Vm vm({{odh::routine_key(4096),program}},world.vm_memory(),{},player.primitive());
        require(player.bind(vm),"bind real absence playback VM");vm.start(odh::routine_key(4096),{1,2,2},{123,456});
        require(vm.run(20)==odh::VmStatus::next_tick&&vm.steps()==1&&vm.stack().back().pc==1&&
            !vm.pending()&&lookups==1&&vm.temps()[1]==0&&vm.stack().back().arguments[0]==123&&
            vm.stack().back().arguments[1]==456&&!player.progress()&&!player.carry_clip(),
            "proven absence branches TRUE only next tick, without frame writes, waits or invented playback in every mode");
        require(vm.run(20)==odh::VmStatus::next_tick&&vm.steps()==1&&lookups==1,
            "missing animation does not implicitly consume another scheduler slice");
        require(vm.resume_next_tick()&&vm.run(20)==odh::VmStatus::returned_true&&vm.temps()[1]==77,
            "ordinary VM next-tick release executes the actual TRUE continuation");
    }
    Fixture held(1);held.start();require(held.apply(16,1).disposition==odh::PrimitiveDisposition::goto_true,
        "establish real primary and carry state before absent requests");
    odh::PlaybackSnapshot before;std::string why;require(held.player.capture_snapshot(before,&why),"capture held state");
    const auto carry=held.player.carry_clip();
    held.player.set_animation_lookup_resolver([](const auto&,auto,auto){return odh::AnimationLookup{Availability::absent,{}, {}};});
    for(const auto flags:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{16},std::uint8_t{17}}){
        const auto outcome=held.apply(flags,9);odh::PlaybackSnapshot after;
        require(outcome.disposition==odh::PrimitiveDisposition::goto_true_next_tick&&!outcome.write&&!outcome.temp_write&&
            held.player.capture_snapshot(after,&why)&&same_primary(before,after)&&before.carry==after.carry&&
            held.player.carry_clip()==carry,
            "absent modes preserve existing primary/carry/gesture/event state and emit no output writes");
    }
    held.player.set_animation_lookup_resolver([](const auto&,auto,auto){return odh::AnimationLookup{Availability::unavailable,{},"provider incomplete"};});
    require(held.apply(1,9).disposition==odh::PrimitiveDisposition::unsupported,"typed provider error remains unavailable");
    held.player.set_animation_lookup_resolver([&](const auto&,auto,auto){return odh::AnimationLookup{Availability::absent,held.a,{}};});
    require(held.apply(1,9).disposition==odh::PrimitiveDisposition::unsupported,"contradictory absence and clip rejects");
    Fixture legacy;legacy.a.reset();require(legacy.apply(1,9).disposition==odh::PrimitiveDisposition::unsupported,
        "legacy null resource remains unavailable rather than proved absence");
    Fixture present;present.player.set_animation_lookup_resolver([&](const auto&,auto,auto){return odh::AnimationLookup{Availability::available,present.a,{}};});
    require(present.apply(1,9).disposition==odh::PrimitiveDisposition::goto_true&&present.player.progress().has_value(),
        "typed known presence still starts actual playback");
}
int main(){try{
    ordinary_animation_absence_tests();
    source_reset_preserves_carry_and_hands();
    prepared_carry_transfer_tests();
    route_animation_tests();
    immediate_preemption_tests();
    default_slot_carry_tests();synchronous_created_carry_tests();
    {Fixture source(1);source.a->motions[0].frames=1;source.xevt(0,"7");source.start();source.player.tick(1);
     odh::PlaybackSnapshot saved;std::string why;require(source.player.capture_snapshot(saved,&why)&&saved.state&&saved.state->resource.id==1&&
        saved.state->resource.caller==1&&saved.state->resource.callee==2&&saved.state->resource.stack_object==2&&saved.last_tick==1,
        "capture looping playback with stable opcode resource key");
     Fixture resumed(1);resumed.a->motions[0].frames=1;resumed.xevt(0,"7");
     require(resumed.vm.restore_snapshot(source.vm.snapshot()),"restore loop VM");
     require(resumed.player.restore_snapshot(saved,resumed.vm,[&](const auto& key){return key.kind==odh::PlaybackResourceKey::Kind::animation&&key.source==0&&key.id==1?resumed.a:std::shared_ptr<const odh::AnimationHeader>{};},&why),
        "restore looping playback into fresh controller");
     source.player.tick(2);resumed.player.tick(2);
     require(source.player.progress()->frame==resumed.player.progress()->frame&&source.player.progress()->queued_events==resumed.player.progress()->queued_events&&
        source.player.hands()->left==resumed.player.hands()->left,"restored loop matches uninterrupted next tick and event delivery");}
    {Fixture source;source.xevt(0,"100");source.start();source.player.tick(1);
     odh::PlaybackSnapshot saved;std::string why;require(source.player.capture_snapshot(saved,&why)&&saved.state&&saved.state->wait&&
        !saved.state->queue.empty(),"capture pending animation wait with queued xevt");
     Fixture resumed;resumed.xevt(0,"100");
     require(resumed.vm.restore_snapshot(source.vm.snapshot()),"restore waiting animation VM");
     require(resumed.player.restore_snapshot(saved,resumed.vm,[&](const auto& key){return key.kind==odh::PlaybackResourceKey::Kind::animation&&key.id==1?resumed.a:std::shared_ptr<const odh::AnimationHeader>{};},&why),
        "restore pending animation wait and queue");
     auto wrong=saved;wrong.state->actor_generation++;
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),"stale actor generation rejected");
     wrong=saved;wrong.state->low=999;
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),"invalid event cursor rejected");
     wrong=saved;wrong.state->wait->ticket++;
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),"mismatched pending VM ticket rejected");
     wrong=saved;wrong.state->wait->expected++;
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),"mismatched pending VM output rejected");
     for(const auto flags:{std::uint8_t{1},std::uint8_t{16}}){
         Fixture nonblocking(flags);auto vm_state=source.vm.snapshot();
         vm_state.pending->instruction.instruction.operands[5]=flags;
         require(nonblocking.vm.restore_snapshot(vm_state),"construct malformed nonblocking animation wait fixture");
         require(!nonblocking.player.restore_snapshot(saved,nonblocking.vm,[&](const auto&){return nonblocking.a;},&why),
            "nonblocking mode cannot restore an opcode-44 wait");
     }
     wrong=saved;wrong.state->resource.id++;
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),"mismatched pending animation resource id rejected");
     wrong=saved;wrong.state->resource.callee++;
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),"mismatched pending animation context rejected");
     wrong=saved;wrong.state->end=true;
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),"terminal marker inside clip rejected");
     wrong=saved;wrong.state->end=true;wrong.state->loop=true;
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),"looping terminal marker rejected");
     require(!resumed.player.restore_snapshot(saved,resumed.vm,[&](const auto&){return clip(9);},&why),"mismatched resolved clip rejected");
     odh::PlaybackSnapshot before_failed_restore,after_failed_restore;
     require(resumed.player.capture_snapshot(before_failed_restore,&why),"capture active controller before failed-restore checks");
     odh::PlaybackSnapshot missing_state;
     require(!resumed.player.restore_snapshot(missing_state,resumed.vm,[&](const auto&){return resumed.a;},&why),
        "snapshot without playback state cannot discard pending opcode-44 VM wait");
     require(!resumed.player.restore_snapshot(wrong,resumed.vm,[&](const auto&){return resumed.a;},&why),
        "invalid wait output restore rejected without replacing active controller");
     require(resumed.player.capture_snapshot(after_failed_restore,&why)&&after_failed_restore.state&&
        before_failed_restore.state&&after_failed_restore.state->frame==before_failed_restore.state->frame&&
        after_failed_restore.state->queue==before_failed_restore.state->queue&&
        after_failed_restore.state->wait->ticket==before_failed_restore.state->wait->ticket,
        "failed restore leaves previously active playback unchanged");
     source.player.tick(2);resumed.player.tick(2);
     require(source.vm.snapshot().stack.back().arguments==resumed.vm.snapshot().stack.back().arguments&&
        source.vm.status()==resumed.vm.status()&&source.player.progress()->frame==resumed.player.progress()->frame,
        "restored queued xevt delivers same VM write and continuation");
     source.vm.run(20);resumed.vm.run(20);source.player.tick(3);resumed.player.tick(3);
     require(source.vm.status()==resumed.vm.status()&&source.vm.temps()==resumed.vm.temps(),"restored animation wait continues identically");
    }
    {Fixture source;source.a->motions[0].time_properties={{{0,{{"righthand","1"}}}}};source.start();
     odh::PlaybackSnapshot saved;std::string why;require(source.player.capture_snapshot(saved,&why)&&saved.state&&saved.hands,
        "capture active animation with required hand state");
     Fixture target;target.a->motions[0].time_properties={{{0,{{"righthand","1"}}}}};target.vm.restore_snapshot(source.vm.snapshot());
     saved.hands.reset();require(!target.player.restore_snapshot(saved,target.vm,[&](const auto&){return target.a;},&why),
        "active animation without hand state is rejected before event tick");}
    {Fixture source(0,0,0);source.world.set_person_data(1,0,2);source.start();
     odh::PlaybackSnapshot saved;std::string why;require(source.player.capture_snapshot(saved,&why)&&saved.state&&saved.state->resource.kind==odh::PlaybackResourceKey::Kind::posture_reset&&saved.state->resource.posture==2,
        "posture reset snapshot keeps normalized posture resource key");
     Fixture resumed(0,0,0);resumed.world.set_person_data(1,0,2);resumed.vm.restore_snapshot(source.vm.snapshot());
     require(resumed.player.restore_snapshot(saved,resumed.vm,[&](const auto& key){return key.kind==odh::PlaybackResourceKey::Kind::posture_reset&&key.posture==2?resumed.reset_a:std::shared_ptr<const odh::AnimationHeader>{};},&why),
        "posture reset resource key resolves on restore");}
    {Fixture source;source.start();Fixture target;target.vm.restore_snapshot(source.vm.snapshot());
     odh::PlaybackSnapshot saved;std::string why;
     require(!target.player.capture_snapshot(saved,&why),"capture rejects opcode-44 VM wait missing its controller state");
     require(!target.player.restore_snapshot(saved,target.vm,[&](const auto&){return target.a;},&why),
        "restore rejects opcode-44 VM wait when snapshot has no controller state");}
    {Fixture f;f.xevt(0,"100");f.start();const auto ticket=f.vm.pending()->ticket;
     require(f.player.tick(1)&&f.vm.status()==odh::VmStatus::waiting&&f.player.progress()->queued_events==1,"event queues after frame advance");
     require(!f.player.tick(1)&&!f.player.tick(3)&&f.vm.pending()->ticket==ticket,"duplicate and gap ticks rejected");
     require(f.player.tick(2)&&f.vm.stack().back().arguments[0]==100&&f.vm.stack().back().pc==1,"prior event delivers false with Args0 code");
     f.vm.run(20);require(f.vm.temps()[0]==100&&f.vm.status()==odh::VmStatus::waiting&&f.player.progress()->frame>2,"reentry retains clip and frame");
     f.player.tick(3);require(f.player.progress()->end&&f.vm.status()==odh::VmStatus::waiting,"end awaits next thread phase");
     f.player.tick(4);require(f.vm.status()==odh::VmStatus::returned_true&&!f.player.progress(),"true after event and end consumed");}
    {Fixture f(32);f.xevt(0,"-2");f.start();f.player.tick(1);f.player.tick(2);
     require(f.vm.stack().back().locals[0]==-2&&f.vm.stack().back().arguments[0]==0,"local destination independent of Args0");}
    {Fixture f(0,3);f.a->motions[0].frames=1;f.xevt(0,"100");f.start();f.player.tick(1);f.player.tick(2);
     std::vector<int> codes{f.vm.stack().back().arguments[0]};
     f.vm.run(20,[&](const auto& e){if(e.pc==1)codes.push_back(f.vm.stack().back().arguments[0]);});
     require(codes==std::vector<int>({100,100,0,1,2})&&f.vm.status()==odh::VmStatus::returned_true,"xevt100 does not increment count; synthetic expected events precede true");}
    {Fixture f;f.a->motions[0].time_properties={{{0,{{"xevt","1"}}},{0,{{"xevt","2"}}}}};f.start();f.player.tick(1);f.player.tick(2);
     f.vm.run(20);require(f.vm.temps()[0]==2&&f.vm.status()==odh::VmStatus::waiting,"simultaneous events drain in file order on reentry");}
    {Fixture f(2);f.a->motions[0].frames=1;f.start();f.player.tick(1);
     require(f.player.progress()->frame<0&&!f.player.progress()->end,"negative fractional frame truncates to zero");
     f.player.tick(2);require(f.player.progress()->end,"backwards reaches end at negative integral frame");
     f.player.tick(3);require(f.vm.status()==odh::VmStatus::returned_true,"backwards final completion");}
    {Fixture f(64);f.world.declare_object_word(1,17,1);f.start();f.player.tick(1);
     require(f.player.progress()->frame>2.3f&&f.player.progress()->frame<2.5f,"hurryable and WalkStyle1 double speed");}
    {Fixture f(4,0,0);f.start({9});require(f.selected==9&&f.source==65536,"IDFromParam maps Object to StackObject enum65536");}
    {Fixture f;f.start();f.vm.start(odh::routine_key(4096),{1,2,2});
     require(!f.player.tick(1)&&!f.player.progress()&&f.vm.status()==odh::VmStatus::running,"restart cancels pending animation without branching new execution");
     f.vm.run(20);require(f.player.progress()->frame==0,"same clip on restarted VM begins at frame zero");}
    {Fixture f;f.xevt(0,"100");f.start();f.player.tick(1);f.player.tick(2);
     f.vm.start(odh::routine_key(4096),{1,2,2});f.vm.run(20);
     require(f.player.progress()->frame==0&&f.player.progress()->queued_events==0,"restart after event invalidates persistent clip state");}
    {Fixture f;f.xevt(0,"100");f.start();f.player.tick(1);f.player.tick(2);f.a=clip(6);f.vm.run(20);
     require(f.player.progress()->frame==0&&f.player.progress()->frames==6,"different immutable clip replaces prior state on reentry");}
    {Fixture f(2);f.a->motions[0].time_properties={{{20,{{"xevt","1"}}},{80,{{"xevt","2"}}}}};f.start();f.player.tick(1);f.player.tick(2);
     require(f.vm.stack().back().arguments[0]==2,"backwards consumes events from end of file order");}
    {Fixture f;f.start();f.world.remove(1);f.world.create(1,odh::EntityKind::avatar,{0,88,0});
     require(f.player.tick(1)&&!f.player.progress()&&f.vm.stack().back().pc==1,"reused actor generation cancels wait");}
    {Fixture f;f.start();f.player.cancel();require(!f.player.progress()&&!f.vm.pending()&&f.vm.stack().back().pc==1,"host cancellation drains matching ticket");}
    {Fixture f(0,0,1,{4096,1048576,1});f.start();f.player.tick(1);f.player.tick(2);
     require(!f.player.progress()&&f.vm.stack().back().pc==1&&!f.vm.pending(),"tick budget cancels without leaving orphan wait");}
    {Fixture f(0,0,1,{0,1048576,10});f.xevt(0,"1");f.start();
     require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress(),"event budget rejects before creating clip");}
    {Fixture f;f.a->motions[0].frames=1048577;f.start();
     require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress(),"frame bound checked before progression");}
    {Fixture f(16);f.start();require(f.vm.status()==odh::VmStatus::returned_true&&!f.player.progress()&&f.player.carry_clip()==f.a,
        "carry-only mode retains resolved resource without replacing primary Playback");}
    {Fixture source(16);source.start();odh::PlaybackSnapshot saved;std::string why;
     require(source.player.capture_snapshot(saved,&why)&&saved.carry&&saved.carry->id==1,"capture stable carry resource key");
     Fixture resumed(16);resumed.vm.restore_snapshot(source.vm.snapshot());
     require(resumed.player.restore_snapshot(saved,resumed.vm,[&](const auto& key){return key.kind==odh::PlaybackResourceKey::Kind::animation&&key.id==1?resumed.a:std::shared_ptr<const odh::AnimationHeader>{};},&why)&&
        resumed.player.carry_clip()==resumed.a,"restore carry resource through matching resolver");}
    {Fixture f(17);f.start();require(f.vm.status()==odh::VmStatus::waiting&&f.player.progress(),
        "carry-and-play mode clears carry and starts primary Playback");}
    {Fixture f(128);f.start();require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress(),
        "unknown animation flag remains unsupported");}
    {Fixture f(0,0,0,{},false);f.start();require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress(),"reset not falsely completed without explicit resolver");}
    for(const auto value:{1,2,0,-1,4}){Fixture f(0,0,0);f.world.set_person_data(1,0,static_cast<std::int16_t>(value));f.world.set_person_data(1,42,1);f.start();
     require(f.posture==((value==1||value==2)?value:3),"reset normalizes only other postures to standing loop3");
     require(f.vm.status()==odh::VmStatus::returned_true&&!f.vm.pending()&&f.player.progress()->loop&&f.player.progress()->frame==0,"reset starts loop and returns true immediately");
     require(f.world.find(1)->person_data[42]==4&&f.world.find(1)->person_data[0]==value,"reset changes HeadSeekState42 only and preserves Posture0");
     f.player.tick(1);require(f.vm.status()==odh::VmStatus::returned_true&&f.player.progress()->frame>1.1f,"reset loop persists after VM return");}
    {Fixture f(64,0,0);f.world.declare_object_word(1,17,1);f.world.set_person_data(1,42,2);f.start();f.player.tick(1);
     require(std::abs(f.player.progress()->frame-1.2f)<0.0001f&&f.world.find(1)->person_data[42]==2,"reset ignores Hurryable and preserves non-one HeadSeekState");}
    {Fixture f(2,0,0);f.reset_a->motions[0].frames=1;f.start();
     require(f.player.progress()->frame==1&&f.player.progress()->loop,"backwards reset starts at NumFrames");
     f.player.tick(1);require(std::abs(f.player.progress()->frame+0.2f)<0.0001f&&!f.player.progress()->end,"backwards fractional negative truncates before loop end");
     f.player.tick(2);require(std::abs(f.player.progress()->frame+0.4f)<0.0001f&&!f.player.progress()->end,"backwards wraps once with fractional overshoot");}
    {Fixture f(1);f.a->motions[0].frames=1;f.xevt(0,"1");f.start();int observed=0;
     require(f.vm.status()==odh::VmStatus::returned_true&&!f.vm.pending()&&f.player.progress()->loop,"mode1 starts primary loop without wait");
     for(std::uint64_t t=1;t<=8;++t)f.player.tick(t,[&](const auto&){++observed;});
     require(observed==1&&f.player.progress()->queued_events==1&&!f.player.progress()->end,"loop events are consumed once and retained queue stays bounded after VM return");
     require(std::abs(f.player.progress()->frame-1.6f)<0.0001f,"one-frame clip wraps only once per tick even beyond one frame");
     require(f.apply(1,1).disposition==odh::PrimitiveDisposition::goto_true&&f.player.progress()->frame==0&&f.player.progress()->queued_events==0,"mode1 replaces same immutable clip and clears consumed events");
     f.player.tick(9,[&](const auto&){++observed;});require(observed==2,"replacement begins fresh event cursor");}
    {Fixture f(67);f.world.declare_object_word(1,17,1);f.start();f.player.tick(1);
     require(f.player.progress()->loop&&std::abs(f.player.progress()->frame-0.6f)<0.0001f,"mode1 honors backwards and hurryable doubling");}
    {Fixture f(33);f.start();require(f.vm.status()==odh::VmStatus::returned_true,"mode1 does not require unused local event destination");}
    {Fixture f(1);f.start();f.player.tick(1);f.vm.start(odh::routine_key(4096),{1,2,2});
     require(!f.player.tick(2)&&!f.player.progress(),"VM restart invalidates returned loop");}
    {Fixture f(1,0,1,{1,1048576,2});f.xevt(0,"100");f.start();f.player.tick(1);f.player.tick(2);
     require(f.player.progress()->queued_events==1,"nonconsuming loop queue respects exact event budget");
     require(f.player.tick(3)&&!f.player.progress()&&!f.vm.pending(),"loop tick budget cancels without creating ticket");}
    for(const auto failure:{0,2,3,4}){Fixture f(1);f.start();f.player.tick(1);f.world.set_person_data(1,42,1);
     if(failure==0)f.reset_a.reset();
     if(failure==1){f.world.create(2,odh::EntityKind::object);f.world.attach(1,0,2);}
     if(failure==2)f.reset_a->motions[0].frames=0;
     if(failure==3)f.reset_a->motions[0].time_properties={{{0,{{"dress","../invalid"}}}}};
     if(failure==4)f.reset_a->motions[0].frames=1048577;
     const auto before=f.player.progress();const auto world_before=*f.world.find(1);
     require(f.apply(0).disposition==odh::PrimitiveDisposition::unsupported,"invalid reset resources remain explicitly unsupported");
     const auto after=f.player.progress();const auto* actor=f.world.find(1);
     require(after&&after->frame==before->frame&&after->frames==before->frames&&after->name==before->name&&after->loop==before->loop,"failed reset preserves existing primary clip");
     require(actor->person_data==world_before.person_data&&actor->slots==world_before.slots&&actor->object_words==world_before.object_words,"failed reset does not alter world state");}
    {Fixture f(1,0,1,{},false);f.start();f.world.set_person_data(1,42,1);const auto before=f.player.progress();
     require(f.apply(0).disposition==odh::PrimitiveDisposition::unsupported&&f.player.progress()->frame==before->frame&&f.world.find(1)->person_data[42]==1,"missing reset resolver is atomic with existing loop");}
    {Fixture f(0,0,0,{},true,{0,88,0});f.world.set_person_data(1,42,1);f.start();
     require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress()&&f.posture==-1&&f.world.find(1)->person_data[42]==1,"absent hand slot is unknown and fails before reset resolution");}
    {Fixture f(0,0,0,{},true,{0,42,1});f.start();require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress(),"reset requires explicit HeadSeekState bank entry");}
    {Fixture f;f.world.create(2,odh::EntityKind::object);f.world.set_position(1,{16,16,1,3});
     f.world.set_position(2,{16,16,1,3});f.world.attach(1,0,2);
     const auto reset=f.apply(0);
     require(reset.disposition==odh::PrimitiveDisposition::goto_true,
             "posture reset succeeds while the avatar holds an object");
     require(f.carry_resolved&&f.player.carry_clip()==f.reset_a,
             "held object installs the default carry animation");
     require(f.world.find(1)->slots[0]==2&&f.player.progress()->loop,
             "posture reset preserves the held object and starts the posture loop");}
    for(const auto mode:{1,16}){Fixture f(static_cast<std::uint8_t>(mode),0,0);f.world.set_person_data(1,0,2);f.start();
     require(f.vm.status()==odh::VmStatus::returned_true&&!f.vm.pending()&&f.player.progress()->loop&&f.posture==2,"ID0 modes1 and2 use ordinary posture reset");}
    {Fixture f(0,0,1,{4096,0xffffffffU,10});f.a->motions[0].frames=0xffffffffU;f.start();
     require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress(),"host frame limits cannot permit clips beyond signed reference frame range");}
    {Fixture f(17,0,0);f.start();require(f.vm.status()==odh::VmStatus::returned_true&&!f.player.progress(),"ID0 mode3 cancels carry without starting primary animation");}
    {Fixture f;f.a->motions[0].time_properties={{{0,{{"sound","unresolved_optional"},{"xevt","7"},{"lefthand","2"}}}}};
     f.start();require(f.vm.status()==odh::VmStatus::waiting&&f.player.progress(),"optional sound allows silent animation preparation");
     int notifications=0;
     f.player.tick(1,[&](const odh::PlaybackEvent& event){++notifications;
         require(event.actor==1&&event.actor_generation==f.world.find(1)->generation&&
                 event.vm_generation==f.vm.run_generation()&&event.properties.front().second=="unresolved_optional",
                 "playback stamps observer event with actual actor and VM incarnation");
         require(static_cast<int>(f.player.hands()->left)==2,"optional sound coexists with committed hand gesture");});
     f.player.tick(2);
     require(notifications==1&&f.vm.stack().back().arguments[0]==7,"optional sound leaves xevt delivery intact without backend");}
    {Fixture source(1);source.a->motions[0].frames=1;
     source.a->motions[0].time_properties={{{0,{{"sound","once"}}}}};source.start();int before=0;
     source.player.tick(1,[&](const auto&){++before;});
     odh::PlaybackSnapshot saved;std::string why;
     require(before==1&&source.player.capture_snapshot(saved,&why),"capture cursor after optional sound notification");
     Fixture resumed(1);resumed.a->motions[0].frames=1;resumed.a->motions[0].time_properties=source.a->motions[0].time_properties;
     require(resumed.vm.restore_snapshot(source.vm.snapshot())&&
             resumed.player.restore_snapshot(saved,resumed.vm,[&](const auto&){return resumed.a;},&why),
             "restore consumed optional sound cursor");
     int after=0;resumed.player.tick(2,[&](const auto&){++after;});
     require(after==0,"snapshot resume does not replay previously emitted optional sound");}
    {Fixture f;f.a->motions[0].time_properties={{{0,{{"dress","Accessory"}}}, {50,{{"undress","Accessory"}}}}};f.start();
     require(f.vm.status()==odh::VmStatus::waiting&&f.player.progress()&&f.world.find(1)->bound_appearances.empty(),"dress waits for authored event frame");
     f.player.tick(1);
     require(f.world.find(1)->bound_appearances==std::vector<std::string>{"Accessory"}&&f.world.find(1)->avatar_appearance->accessory_assets==std::optional<std::vector<std::string>>{{"Accessory.apr"}},"dress preserves exact raw key and resolves separate .apr visual asset");
     odh::PlaybackSnapshot saved;std::string why;require(f.player.capture_snapshot(saved,&why),"capture dressed event cursor");Fixture resumed(0,0,1,{},true,{0,88,1},false);
     resumed.a->motions[0].time_properties=f.a->motions[0].time_properties;
     require(resumed.world.restore_snapshot(f.world.snapshot()),"fresh dressed World restore");
     require(resumed.vm.restore_snapshot(f.vm.snapshot()),"fresh dressed VM restore");
     if(!resumed.player.restore_snapshot(saved,resumed.vm,[&](const auto& key){return key.kind==odh::PlaybackResourceKey::Kind::posture_reset?resumed.reset_a:resumed.a;},&why))
        throw std::runtime_error("fresh dressed Playback restore: "+why);
     f.player.tick(2);resumed.player.tick(2);
     require(f.world.find(1)->bound_appearances.empty()&&f.world.find(1)->avatar_appearance->accessory_assets->empty()&&
        resumed.world.find(1)->avatar_appearance==f.world.find(1)->avatar_appearance&&resumed.world.find(1)->bound_appearances.empty(),"undress removes raw key and visual resource identically after restore");}
    for(const auto* metadata:{"foo","unrecognized-metadata"}){
     Fixture f;f.a->motions[0].time_properties={{{0,{{metadata,"retained"},{"xevt","7"},{"lefthand","2"}}}}};
     f.start();require(f.vm.status()==odh::VmStatus::waiting&&f.player.progress(),"unqueried metadata does not reject an original animation");
     f.player.tick(1);f.player.tick(2);
     require(f.vm.stack().back().arguments[0]==7&&static_cast<int>(f.player.hands()->left)==2,
         "unqueried metadata leaves xevt and hand dispatch intact");}
    {Fixture f;f.a.reset();f.start();require(f.vm.status()==odh::VmStatus::fault,"missing clip is explicit fault");}
    {Fixture f;f.a->motions[0].time_properties={{{1000,{{"xevt","1"}}},{0,{{"xevt","2"}}}}};f.start();f.player.tick(1);
     require(f.player.progress()->queued_events==0,"unsorted later event remains blocked by file-order head");}
    {Fixture f;f.xevt(0,"bad");f.start();f.player.tick(1);f.player.tick(2);
     require(f.vm.stack().back().arguments[0]==0,"invalid short xevt falls back to zero per reference");}
    {Fixture f;odh::Vm other({{odh::routine_key(4096),routine()}},f.world.vm_memory(),{},f.player.primitive());other.start(odh::routine_key(4096),{1,2,2});
     require(other.run(1)==odh::VmStatus::fault&&!f.player.progress(),"foreign VM identity rejected");}
    {Fixture f;f.xevt(0,"100");f.start();bool guarded=false;
     f.player.tick(1,[&](const auto&){try{f.player.tick(2);}catch(const std::logic_error&){guarded=true;}});
     require(guarded&&f.player.progress()->queued_events==1,"observer cannot reenter ticks");}
    {Fixture f;require(!f.player.sample(),"no sample before animation starts");f.start();
     const auto s=f.player.sample();require(s&&s->clip.get()==f.a.get()&&s->actor==1&&s->actor_generation==f.world.find(1)->generation&&s->vm_generation==f.vm.run_generation(),"typed sample retains resource and actor/run identity");
     require(s->frame==f.player.progress()->frame&&s->frames==f.player.progress()->frames,"sample copies current frame metadata");
     f.player.tick(1);require(f.player.sample()->frame>s->frame&&s->frame==0,"previous immutable sample does not advance with controller");
     f.player.cancel();require(!f.player.sample()&&s->clip->name==f.a->name,"cancel invalidates controller sample while retained resource survives");}
    {Fixture f(1);f.start();require(f.player.sample()->loop&&f.vm.status()==odh::VmStatus::returned_true,"nonblocking terminal loop remains drawable");
     const auto before=f.player.progress();f.vm.start(odh::routine_key(4096),{1,2,2});
     require(!f.player.sample()&&f.player.progress()->frame==before->frame,"sample rejects restarted run before tick without mutation");}
    {Fixture f(1);f.start();const auto generation=f.player.sample()->actor_generation;f.world.remove(1);f.world.create(1,odh::EntityKind::avatar,{0,88,1});
     require(f.world.find(1)->generation!=generation&&!f.player.sample(),"sample rejects removed/reused actor before tick");}
    {Fixture f;require(!f.player.hands(),"no inferred gestures before primary clip");
     const auto before=f.world.find(1)->person_data;
     f.a->motions[0].time_properties={{{0,{{"lefthand","1"},{"righthand","2"},{"xevt","7"}}},
                                     {70,{{"lefthand","3"},{"righthand","0"}}}}};
     f.start();const auto initial=f.player.sample();
     require(initial->hands.left==odh::HandGesture::idle&&initial->hands.right==odh::HandGesture::idle,"clip start explicitly resets both hands");
     bool observed=false;f.player.tick(1,[&](const auto&){const auto h=f.player.hands();observed=h&&h->left==odh::HandGesture::pointing&&h->right==odh::HandGesture::fist;});
     require(observed&&f.player.sample()->hands.left==odh::HandGesture::pointing,"event observer and render sample see committed gestures");
     require(initial->hands.left==odh::HandGesture::idle&&f.world.find(1)->person_data==before,"previous sample and raw person banks unchanged");
     f.player.tick(2);require(f.player.hands()->left==odh::HandGesture::none&&f.player.hands()->right==odh::HandGesture::idle,"later timed event changes both hands");
     f.vm.run(20);f.player.tick(3);f.player.tick(4);
     require(f.vm.status()==odh::VmStatus::returned_true&&!f.player.sample()&&f.player.hands()->left==odh::HandGesture::none,"gestures persist after completed clip");}
    for(const auto* valid:{"0","1","2","3"," +2 ","\t1\r\n","-0"}){
     Fixture f;f.a->motions[0].time_properties={{{0,{{"lefthand",valid}}}}};f.start();
     require(f.vm.status()==odh::VmStatus::waiting,"supported gesture numeric syntax");f.player.tick(1);
     require(f.player.hands().has_value(),"valid gesture committed");}
    for(const auto* invalid:{"","bad","1x","1.0","4","-1","32768","+","+-0"," 1 2 "}){
     Fixture f;f.a->motions[0].time_properties={{{0,{{"lefthand",invalid}}}}};f.start();
     require(f.vm.status()==odh::VmStatus::fault&&!f.player.sample()&&!f.player.hands(),"invalid gesture rejects before primary state or hand mutation");}
    {Fixture f;f.a->motions[0].time_properties={{{0,{{"lefthand","1"},{"lefthand","bad"},{"righthand","2"},{"righthand","0"}}}}};
     f.start();f.player.tick(1);require(f.player.hands()->left==odh::HandGesture::pointing&&f.player.hands()->right==odh::HandGesture::fist,"first hand property value wins as reference keyed lookup");
     const auto hand=f.player.hands();f.player.cancel();
     require(!f.player.sample()&&f.player.hands()->left==hand->left,"cancel preserves current visual hand state");
     f.vm.start(odh::routine_key(4096),{1,2,2});require(!f.player.hands(),"stale run gesture identity rejected before next tick");}
    {Fixture f(1);f.a->motions[0].time_properties={{{0,{{"lefthand","3"},{"righthand","1"}}}}};f.start();f.player.tick(1);
     require(f.player.hands()->left==odh::HandGesture::none,"loop can hide hand");
     f.reset_a->motions[0].time_properties={{{0,{{"lefthand","bad"}}}}};
     require(f.apply(0).disposition==odh::PrimitiveDisposition::unsupported&&f.player.hands()->left==odh::HandGesture::none,"invalid reset preserves previous gestures");
     f.reset_a->motions[0].time_properties.clear();require(f.apply(0).disposition==odh::PrimitiveDisposition::goto_true,"valid posture reset accepted");
     require(f.player.hands()->left==odh::HandGesture::idle&&f.player.hands()->right==odh::HandGesture::idle,"posture reset restores idle hands");
     f.world.remove(1);f.world.create(1,odh::EntityKind::avatar,{0,88,1});require(!f.player.hands(),"removed/reused actor cannot inherit gestures");}
    {Fixture f(2);f.a->motions[0].time_properties={{{0,{{"lefthand","1"}}},{60,{{"righthand","3"}}}}};f.start();f.player.tick(1);
     require(f.player.hands()->left==odh::HandGesture::idle&&f.player.hands()->right==odh::HandGesture::none,"backwards consumes later event first");}
    for(const bool missing_clip:{false,true}){
        Fixture f(1);f.start();const auto before=f.player.sample()->clip;
        f.player.set_reset_lookup_resolver([&](const auto&,auto){return odh::ResetAnimationLookup{
            odh::ResetAnimationAvailability::available,missing_clip?std::shared_ptr<const odh::AnimationHeader>{}:f.reset_a,{}};});
        const auto result=f.apply(0);
        require(result.disposition==(missing_clip?odh::PrimitiveDisposition::unsupported:odh::PrimitiveDisposition::goto_true)&&
            f.player.sample()->clip==(missing_clip?before:f.reset_a),"typed available reset requires an actual clip and retains ordinary reset preparation semantics");
    }
    for(const auto availability:{odh::ResetAnimationAvailability::absent,odh::ResetAnimationAvailability::unavailable}){
        Fixture f(1);f.a->motions[0].time_properties={{{0,{{"lefthand","3"},{"righthand","1"}}}}};f.start();f.player.tick(1);
        require(f.apply(16,1).disposition==odh::PrimitiveDisposition::goto_true,"install actual carry mode before typed posture reset");
        f.world.set_person_data(1,42,1);const auto previous=f.player.progress();const auto carry=f.player.carry_clip();const auto hands=f.player.hands();
        f.player.set_reset_lookup_resolver([&](const auto&,std::int16_t posture){require(posture==3,"reset retains normalized source posture");
            return odh::ResetAnimationLookup{availability,{},"explicit source owner unavailable"};});
        const auto result=f.apply(0);
        if(availability==odh::ResetAnimationAvailability::unavailable){
            require(result.disposition==odh::PrimitiveDisposition::unsupported&&f.player.progress()->name==previous->name&&
                f.world.find(1)->person_data[42]==1,"unknown reset authority refuses without clearing current animation or changing head seek");
        }else{
            require(result.disposition==odh::PrimitiveDisposition::goto_true&&!f.player.progress()&&!f.player.sample_collection()&&
                f.world.find(1)->person_data[42]==4,"authored absent reset clears primary animation and applies source head-seek transition");
            odh::PlaybackSnapshot saved;std::string why;
            require(f.player.capture_snapshot(saved,&why)&&!saved.state&&!saved.source_walk&&saved.carry&&saved.hands,
                "absent primary reset captures no stale pose while retaining carry and hands");
            Fixture resumed(1);require(resumed.vm.restore_snapshot(f.vm.snapshot()),"restore same source VM for absent animation state");
            require(resumed.player.restore_snapshot(saved,resumed.vm,[&](const auto&){return resumed.a;},&why)&&
                !resumed.player.progress()&&resumed.player.carry_clip(),"fresh controller restores authored absence without resurrecting a primary clip");
            odh::PlaybackSnapshot reopened;require(resumed.player.capture_snapshot(reopened,&why)&&
                same_primary(reopened,saved)&&reopened.carry==saved.carry,
                "restored persisted snapshot preserves absent primary pose, exact carry key and hand state");
        }
        require(f.player.carry_clip()==carry&&f.player.hands()->left==hands->left&&f.player.hands()->right==hands->right,
            "typed absent/unavailable resets preserve the independent carry and hands owners");
    }
    std::cout<<checks<<" playback checks passed (silent synthetic resources only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Playback test failed: "<<e.what()<<'\n';return 1;}}
