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
    std::shared_ptr<odh::AnimationHeader> a=clip(),reset_a=clip();
    std::int16_t posture=-1;
    bool carry_resolved=false;
    std::uint32_t source=0;std::uint16_t selected=0;
    odh::Playback player{world,[&](const auto&,auto scope,auto id){source=scope;selected=id;return a;}};
    odh::Vm vm;
    Fixture(std::uint8_t flags=0,std::uint8_t expected=0,std::uint16_t id=1,odh::PlaybackLimits limits={},bool reset_resources=true,odh::EntityLayout layout={0,88,1}):
        player{world,[&](const auto&,auto scope,auto selected_id){source=scope;selected=selected_id;return a;},limits,reset_resources?odh::AnimationResetResolver{[&](const auto&,auto value){if(value==-1)carry_resolved=true;else posture=value;return reset_a;}}:odh::AnimationResetResolver{}},
        vm({{odh::routine_key(4096),routine(flags,expected,id)}},world.vm_memory(),{},player.primitive()){
        world.create(1,odh::EntityKind::avatar,layout);world.declare_object_word(1,17,0);player.bind(vm);
    }
    void start(const std::vector<std::int16_t>& args={}){vm.start(odh::routine_key(4096),{1,2,2},args);vm.run(20);}
    odh::PrimitiveOutcome apply(std::uint8_t flags,std::uint16_t id=0){
        const odh::VmFrame frame{odh::routine_key(4096),0,{1,2,2},{0,0},{0,0}};
        return player.primitive()(frame,routine(flags,0,id).instructions[0],vm.temps(),{&vm,0});
    }
    void xevt(std::int32_t time,const std::string& value){a->motions[0].time_properties={{{time,{{"xevt",value}}}}};}
};
}
int main(){try{
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
     if(failure==3)f.reset_a->motions[0].time_properties={{{0,{{"sound","missing"}}}}};
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
    {Fixture f;f.a->motions[0].time_properties={{{0,{{"sound","missing"}}}}};f.start();
     require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress(),"missing property service does not silently succeed");}
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
    std::cout<<checks<<" playback checks passed (silent synthetic resources only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Playback test failed: "<<e.what()<<'\n';return 1;}}
