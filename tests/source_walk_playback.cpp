#include "odh/playback.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
std::uint32_t bits(float value){std::uint32_t result;std::memcpy(&result,&value,sizeof(result));return result;}
std::shared_ptr<odh::AnimationHeader> animation(const std::string& name,unsigned frames,bool events,unsigned code){
    auto value=std::make_shared<odh::AnimationHeader>();value->name=name;odh::BoneMotion motion{};motion.frames=frames;
    if(events){odh::AnimationProperties props={{"xevt",std::to_string(code)}};
        if(code==3)props.push_back({"righthand","3"});
        if(code==20)props.push_back({"lefthand","1"});
        if(code==25)props.push_back({"righthand","2"});
        motion.time_properties={{{0,props}}};}
    value->motions.push_back(motion);return value;
}
odh::RoutineSet program(std::uint8_t flags,std::uint16_t id){
    return {{odh::routine_key(4096),{0x8002,{{99,1,255,{}},
        {44,254,1,{static_cast<std::uint8_t>(id),static_cast<std::uint8_t>(id>>8),0,0,0,flags,0,0}}},
        {},odh::Bhav::Metadata{0,4,4,0}}}};
}
struct Fixture {
    odh::World world;
    std::map<std::pair<bool,std::uint16_t>,std::shared_ptr<odh::AnimationHeader>> clips;
    std::shared_ptr<odh::AnimationHeader> carry=animation("carry",4,false,0),other=animation("ordinary",4,false,0);
    bool empty_half=false,unknown_half=false,fail_half=false,throw_resource=false;
    std::function<void()> during_name,during_resource;
    std::vector<std::uint16_t> name_reads,loads;
    odh::Playback player;
    odh::Vm vm;
    Fixture(bool populate=true,bool events=true,std::uint8_t flags=0,std::uint16_t id=1,unsigned full_frames=4):
        player(world,[this](const auto&,auto,auto selected)->std::shared_ptr<const odh::AnimationHeader>{
            if(selected==1)return clips.at({false,3});if(selected==2)return other;return carry;
        },{},[this](const auto&,auto)->std::shared_ptr<const odh::AnimationHeader>{return carry;},
        [this](const auto&,std::uint16_t index,bool swim)->std::shared_ptr<const odh::AnimationHeader>{
            loads.push_back(index);if(during_resource)during_resource();if(throw_resource)throw std::runtime_error("resource throw");
            if(fail_half&&index==25)return {};return clips.at({swim,index});
        },[this](const auto&,std::uint16_t index,bool swim)->std::optional<std::string>{
            name_reads.push_back(index);if(during_name)during_name();
            if(index==25&&unknown_half)return {};if(index==25&&empty_half)return std::string{};
            return clips.at({swim,index})->name;
        }),vm(program(flags,id),world.vm_memory(),{},[this](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
            if(instruction.opcode==99)return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
            return player.primitive()(frame,instruction,temps,dispatch);
        }){
        for(bool swim:{false,true})for(auto index:{std::uint16_t{3},std::uint16_t{20},std::uint16_t{21},std::uint16_t{25}})
            clips[{swim,index}]=animation((swim?"swim-":"walk-")+std::to_string(index),index==3?8:full_frames,events,index);
        if(populate)require(world.create(1,odh::EntityKind::avatar,{0,88,1})&&world.create(2,odh::EntityKind::object)&&
            world.set_position(1,{32,32,1,0})&&world.set_position(2,{32,32,1,0})&&world.set_person_data(1,58,30),"create source walk avatar and explicit resource target");
        require(player.bind(vm),"bind exactly one Playback to the actor VM");
        if(populate)require(vm.start(odh::routine_key(4096),{1,1,2},{0,0,0,0})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::waiting,
            "park the real VM at a safe source continuation before walking service");
    }
    bool start(bool swim=false,std::int16_t style=0,bool pet=false,std::string* why=nullptr){
        return player.start_source_walk_animation(vm.stack().back(),vm,world.find(1)->generation,vm.run_generation(),swim,style,pet,why);
    }
    bool velocity(int value){return player.set_source_walk_velocity(vm.stack().back(),vm,world.find(1)->generation,vm.run_generation(),value);}
    odh::PlaybackSnapshot snapshot(){odh::PlaybackSnapshot result;std::string why;require(player.capture_snapshot(result,&why),why.c_str());return result;}
    std::shared_ptr<const odh::AnimationHeader> resolve(const odh::PlaybackResourceKey& key){
        if(key.kind==odh::PlaybackResourceKey::Kind::source_locomotion||key.kind==odh::PlaybackResourceKey::Kind::route_walk)return clips.at({key.source!=0,key.id});
        if(key.kind==odh::PlaybackResourceKey::Kind::posture_reset)return carry;
        if(key.kind==odh::PlaybackResourceKey::Kind::animation&&key.id==1)return clips.at({false,3});
        if(key.kind==odh::PlaybackResourceKey::Kind::animation&&key.id==3)return carry;
        return other;
    }
    void carry_and_hands(){
        require(world.attach(1,0,2)&&player.attach_default_carry(vm.stack().back(),vm,world.find(1)->generation,vm.run_generation(),2,world.find(2)->generation),
            "attach source default carry before locomotion");
        auto state=snapshot();state.hands=odh::PlaybackHandsSnapshot{{odh::HandGesture::fist,odh::HandGesture::pointing},1,world.find(1)->generation,vm.run_generation()};
        require(player.restore_snapshot(state,vm,[this](const auto& key){return resolve(key);}),"restore explicit native hand gestures independently of locomotion");
    }
    void run_opcode44(){require(vm.complete(vm.pending()->ticket,true)&&vm.run(1)!=odh::VmStatus::fault,"execute actual opcode44 in the same actor run after source walking starts");}
};
bool clip_equal(const odh::PlaybackClipSnapshot& a,const odh::PlaybackClipSnapshot& b){
    return a.resource==b.resource&&a.actor==b.actor&&a.actor_generation==b.actor_generation&&a.vm_generation==b.vm_generation&&
        bits(a.frame)==bits(b.frame)&&bits(a.speed)==bits(b.speed)&&a.frames==b.frames&&a.backwards==b.backwards&&a.end==b.end&&a.loop==b.loop&&
        a.low==b.low&&a.high==b.high&&a.queue==b.queue&&a.events_run==b.events_run&&a.ticks==b.ticks&&bool(a.wait)==bool(b.wait)&&
        (!a.wait||(a.wait->ticket==b.wait->ticket&&a.wait->bank==b.wait->bank&&a.wait->index==b.wait->index&&a.wait->expected==b.wait->expected));
}
bool all_equal(const odh::PlaybackSnapshot& a,const odh::PlaybackSnapshot& b){
    if(bool(a.state)!=bool(b.state)||bool(a.source_walk)!=bool(b.source_walk)||!(a.carry==b.carry)||a.last_tick!=b.last_tick||bool(a.hands)!=bool(b.hands))return false;
    if(a.state&&!clip_equal(*a.state,*b.state))return false;
    if(a.hands&&(a.hands->value.left!=b.hands->value.left||a.hands->value.right!=b.hands->value.right||a.hands->actor!=b.hands->actor||
        a.hands->actor_generation!=b.hands->actor_generation||a.hands->vm_generation!=b.hands->vm_generation))return false;
    if(a.source_walk){if(a.source_walk->weights!=b.source_walk->weights)return false;for(std::size_t n=0;n<2;++n)if(!clip_equal(a.source_walk->secondary[n],b.source_walk->secondary[n]))return false;}
    return true;
}
void collection_frames_events(){
    Fixture f;f.carry_and_hands();const auto carry=f.player.carry_clip();
    require(f.start()&&!f.world.find(1)->static_footprint&&f.player.carry_clip()==carry&&
        f.player.hands()->left==odh::HandGesture::fist&&f.player.hands()->right==odh::HandGesture::pointing,
        "StartWalkAnimation preserves current carry/hands and makes actor footprint dynamic");
    require(!f.player.sample()&&f.player.sample_collection()->count==3,"three-state locomotion cannot masquerade as a single idle render sample");
    const auto initial=f.snapshot();
    require(initial.source_walk&&initial.state->resource.kind==odh::PlaybackResourceKey::Kind::source_locomotion&&initial.state->resource.id==3&&
        initial.state->speed==1&&initial.state->loop&&initial.source_walk->secondary[0].resource.id==20&&initial.source_walk->secondary[1].resource.id==25&&
        initial.source_walk->secondary[0].speed==1.5f&&initial.source_walk->secondary[1].speed==1.5f&&
        initial.source_walk->weights==std::array<float,3>{0,0.66f,0.33f},"source initial idle/full/half resources, speed, loop and raw weights are exact");
    std::vector<unsigned> observed;
    require(f.player.advance(1,[&](const auto& event){observed.push_back(event.layer);}),"advance all source states through one owning Playback tick");
    auto state=f.snapshot();
    require(state.state->frame==0&&state.state->low==0&&state.source_walk->secondary[0].frame==1.5f&&state.source_walk->secondary[1].frame==1.5f&&
        state.state->queue==std::deque<std::int16_t>{20,25}&&state.state->events_run==2&&
        state.source_walk->secondary[0].queue.empty()&&state.source_walk->secondary[1].queue.empty()&&
        state.source_walk->secondary[0].events_run==0&&observed==std::vector<unsigned>{1,2},
        "zero idle weight pauses frame/properties while both secondary xevt events append to CurrentAnimationState in source order");
    require(f.player.hands()->left==odh::HandGesture::pointing&&f.player.hands()->right==odh::HandGesture::fist,"secondary property events update shared avatar hand state");
    require(f.velocity(0)&&f.player.advance(2),"source velocity0 activates idle while stopping both walking clips");
    state=f.snapshot();
    require(state.state->frame==1&&state.source_walk->secondary[0].frame==1.5f&&state.source_walk->secondary[1].frame==1.5f&&
        state.state->queue==std::deque<std::int16_t>{20,25,3}&&f.player.hands()->right==odh::HandGesture::none,
        "idle event runs only when its weight becomes nonzero and paused walking cursors remain exact");
    for(int velocity=0;velocity<=8;++velocity){require(f.velocity(velocity),"accept bounded source velocity");const auto samples=f.player.sample_collection();
        require(samples&&samples->layers[0].weight==(8-velocity)/8.0f&&samples->layers[1].weight==(velocity/8.0f)*0.66f&&
            samples->layers[2].weight==(velocity/8.0f)*0.33f,"render collection exposes source rawweights for prefix cumulative blending");}
    require(!f.velocity(-1)&&!f.velocity(9),"source animation weights reject speed outside0..8");
    require(!f.player.advance(2),"collection cannot advance twice at the same owning tick");
}
void selection_and_early_return(){
    struct Expected{bool swim,pet;std::int16_t style;std::uint16_t full,half;};
    for(const auto selection:std::array<Expected,4>{{{false,false,0,20,25},{false,true,0,20,20},{false,false,1,21,21},{true,true,1,20,21}}}){
        Fixture f(false,false);require(f.world.create(1,odh::EntityKind::avatar,{0,88,1})&&f.world.create(2,odh::EntityKind::object)&&
            f.world.set_position(1,{0,0,1,0})&&f.vm.start(odh::routine_key(4096),{1,1,2},{0,0,0,0})==odh::VmStatus::running&&f.vm.run(1)==odh::VmStatus::waiting,"prepare selection profile");
        require(f.start(selection.swim,selection.style,selection.pet),"start selected source walk/swim profile");const auto state=f.snapshot();
        require(state.source_walk->secondary[0].resource.id==selection.full&&state.source_walk->secondary[1].resource.id==selection.half&&
            state.state->resource.source==static_cast<unsigned>(selection.swim),"pet/full/run and pool WalkStyle0 table selection match the source");
    }
    Fixture fallback;fallback.empty_half=true;
    require(fallback.start()&&fallback.snapshot().source_walk->secondary[1].resource.id==20,"proven empty half name falls back to full20");
    Fixture unknown;unknown.unknown_half=true;
    require(!unknown.start()&&!unknown.player.sample_collection(),"unknown half table entry cannot pretend to be source empty fallback");
    Fixture missing;missing.fail_half=true;
    require(!missing.start()&&!missing.player.sample_collection(),"unresolved nonempty half resource cannot pretend to be source empty fallback");
    Fixture f;require(f.start()&&f.velocity(4)&&f.player.advance(1),"prepare progressed source collection before early return");
    const auto before=f.snapshot();const auto loads=f.loads.size();f.name_reads.clear();f.unknown_half=true;
    require(f.world.set_static_footprint(1,true)&&f.start()&&!f.world.find(1)->static_footprint&&f.loads.size()==loads&&
        f.name_reads==std::vector<std::uint16_t>{3,20}&&all_equal(before,f.snapshot()),
        "source first-two-name early return does not read half entry or restart frames, events or weights, but still clears static footprint");
    require(f.start(false,0,true)&&f.snapshot().source_walk->secondary[1].resource.id==25,
        "early-return deliberately ignores changed pet/half selection when first two names still match");
    f.unknown_half=false;
    require(f.start(false,1)&&f.snapshot().source_walk->secondary[0].resource.id==21&&f.snapshot().state->frame==0,
        "changed full animation name restarts all three clips with source initial state");
}
void fresh_restore_and_validation(){
    Fixture live;live.carry_and_hands();require(live.start()&&live.velocity(4)&&live.player.advance(1),"prepare full collection snapshot");const auto snapshot=live.snapshot();
    Fixture restored(false);
    require(restored.world.restore_snapshot(live.world.snapshot())&&restored.vm.restore_snapshot(live.vm.snapshot())&&
        restored.player.restore_snapshot(snapshot,restored.vm,[&](const auto& key){return restored.resolve(key);})&&all_equal(snapshot,restored.snapshot()),
        "fresh execution graph restores every collection resource/frame/weight/event cursor and carry/hands value");
    require(restored.player.sample_collection()->layers[0].sample.clip!=live.player.sample_collection()->layers[0].sample.clip,
        "reopened graph uses its own immutable animation resources");
    for(std::uint64_t tick=2;tick<=6;++tick){require(live.player.advance(tick)&&restored.player.advance(tick)&&all_equal(live.snapshot(),restored.snapshot()),
        "fresh collection continuation preserves exact looping frames, events and gestures at every owning tick");}
    for(unsigned malformed=0;malformed<7;++malformed){auto bad=snapshot;
        if(malformed==0)bad.source_walk->secondary[1].resource.id=9;
        if(malformed==1)bad.source_walk->secondary[0].queue.push_back(77);
        if(malformed==2)bad.source_walk->secondary[0].actor_generation++;
        if(malformed==3)bad.source_walk->weights[0]=0.123f;
        if(malformed==4)bad.source_walk->secondary[0].speed=1.2f;
        if(malformed==5)bad.source_walk->secondary[0].ticks++;
        if(malformed==6)bad.source_walk.reset();
        const auto before=restored.snapshot();
        require(!restored.player.restore_snapshot(bad,restored.vm,[&](const auto& key){return restored.resolve(key);})&&all_equal(before,restored.snapshot()),
            "malformed collection cannot publish a partial replacement or masquerade as a turn clip");
    }
    Fixture throwing(false);require(throwing.world.restore_snapshot(live.world.snapshot())&&throwing.vm.restore_snapshot(live.vm.snapshot()),"prepare throwing restore graph");
    require(!throwing.player.restore_snapshot(snapshot,throwing.vm,[&](const auto& key)->std::shared_ptr<const odh::AnimationHeader>{
        if(key.kind==odh::PlaybackResourceKey::Kind::source_locomotion&&key.id==20)throw std::runtime_error("throw full");return throwing.resolve(key);
    })&&!throwing.player.sample_collection(),"resource resolver throw cannot publish partial restored collection");
}
void opcode44_and_root_exit(){
    Fixture alias(true,false);require(alias.start(),"start source collection before opcode44 alias");alias.run_opcode44();
    require(alias.vm.status()==odh::VmStatus::waiting&&alias.player.sample_collection()->count==3&&alias.snapshot().state->wait&&
        alias.snapshot().state->resource.kind==odh::PlaybackResourceKey::Kind::animation,
        "actual opcode44 idle alias preserves three clips while binding its native wait to shared primary");
    const auto snapshot=alias.snapshot();Fixture reopened(false,false);
    require(reopened.world.restore_snapshot(alias.world.snapshot())&&reopened.vm.restore_snapshot(alias.vm.snapshot())&&
        reopened.player.restore_snapshot(snapshot,reopened.vm,[&](const auto& key){return reopened.resolve(key);})&&all_equal(snapshot,reopened.snapshot()),
        "opcode44 alias wait plus original secondary resource provenance reopens on exact canonical idle asset");
    Fixture wrong_alias(false,false);require(wrong_alias.world.restore_snapshot(alias.world.snapshot())&&wrong_alias.vm.restore_snapshot(alias.vm.snapshot()),"prepare noncanonical alias graph");
    require(!wrong_alias.player.restore_snapshot(snapshot,wrong_alias.vm,[&](const auto& key)->std::shared_ptr<const odh::AnimationHeader>{
        if(key.kind==odh::PlaybackResourceKey::Kind::source_locomotion&&key.id==3)return std::make_shared<odh::AnimationHeader>(*wrong_alias.clips.at({false,3}));
        return wrong_alias.resolve(key);
    }),"same-name copies do not fake exact immutable opcode44 alias identity");
    Fixture replace(true,false,1,2);replace.carry_and_hands();require(replace.start(),"start collection before actual opcode44 replacement");const auto carry=replace.player.carry_clip();replace.run_opcode44();
    require(replace.player.sample()&&replace.player.sample()->clip==replace.other&&replace.player.sample_collection()->count==1&&
        !replace.snapshot().source_walk&&replace.player.carry_clip()==carry&&replace.player.hands()->left==odh::HandGesture::idle,
        "opcode44 replacement clears all extra clips and resets hands while preserving carry");
    Fixture carry_only(true,false,16,3);require(carry_only.start(),"start collection before mode2 carry");carry_only.run_opcode44();
    require(carry_only.player.sample_collection()->count==3&&carry_only.player.carry_clip()==carry_only.carry,"actual carry-only opcode44 does not clear locomotion collection");
    Fixture end;end.carry_and_hands();require(end.start()&&end.velocity(0)&&end.player.advance(1),"prepare moving idle state for root PreExit");const auto before=end.snapshot();
    require(end.player.end_source_walk_animation(end.vm.stack().back(),end.vm,end.world.find(1)->generation,end.vm.run_generation())&&end.world.find(1)->static_footprint,
        "root PreExit restores static actor footprint");
    const auto after=end.snapshot();
    require(!after.source_walk&&after.state->resource.kind==odh::PlaybackResourceKey::Kind::route_walk&&after.state->resource.id==3&&
        after.state->frame==before.state->frame&&after.state->low==before.state->low&&after.state->queue==before.state->queue&&after.state->events_run==before.state->events_run&&
        after.carry==before.carry&&after.hands->value.right==before.hands->value.right&&end.player.sample_collection()->count==1&&end.player.sample(),
        "root PreExit drops extras and retains primary idle frame, queue, cursors, carry and hands with a valid single-idle resource key");
}
void zero_weight_progress_and_stale_context(){
    Fixture tiny(true,false,0,1,1);
    require(tiny.start()&&tiny.player.advance(1)&&tiny.player.advance(2)&&tiny.snapshot().source_walk->secondary[0].frame==1,
        "source one-wrap rule can leave a tiny fast looping clip at its frame bound");
    require(tiny.velocity(0)&&tiny.player.advance(3)&&tiny.snapshot().source_walk->secondary[0].frame==0,
        "SilentFrameProgress still wraps a zero-weight state even while frame/event advancement is paused");
    Fixture f;require(f.start(),"start source collection before rejected callback changes");const auto before=f.snapshot();
    bool changed=false;f.during_name=[&]{if(!changed){changed=true;require(f.vm.update_pending(f.vm.pending()->ticket,{odh::VmFrameBank::arguments,0,55}),"mutate exact source frame during name callback");}};
    require(!f.start()&&f.player.sample_collection()&&all_equal(before,f.snapshot()),"name callback VM frame mutation cannot publish replacement clips");
    Fixture stale;bool recycled=false;stale.during_resource=[&]{if(!recycled){recycled=true;require(stale.world.remove(1)&&stale.world.create(1,odh::EntityKind::avatar,{0,88,1})&&
        stale.world.set_position(1,{32,32,1,0}),"recycle source actor during resource resolution");}};
    require(!stale.start()&&!stale.player.sample_collection(),"resource callback recycled actor generation rejects before collection publication");
    Fixture restart;require(restart.start(),"start collection before VM restart");
    require(restart.vm.start(odh::routine_key(4096),{1,1,2},{0,0,0,0})==odh::VmStatus::running&&!restart.player.sample_collection(),"read-only collection sample rejects stale VM generation before any animation tick");
}
}
int main(){try{collection_frames_events();selection_and_early_return();fresh_restore_and_validation();opcode44_and_root_exit();zero_weight_progress_and_stale_context();
    std::cout<<checks<<" source walk playback checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"source walk playback: "<<error.what()<<'\n';return 1;}}
