// SPDX-License-Identifier: MPL-2.0
#include "odh/playback.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "saved_avatar_containment_fixture.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
const auto key=odh::routine_key(4096);
odh::RoutineSet program(std::uint16_t mode=0){
    return {{key,odh::Bhav{0x8002,{{47,254,255,{static_cast<std::uint8_t>(mode),0,0xfe,0xff,0,0,42,99}}},{},
        odh::Bhav::Metadata{0,2,0,0}}}};
}
std::shared_ptr<odh::AnimationHeader> clip(unsigned frames=8){
    auto value=std::make_shared<odh::AnimationHeader>();value->name="reach";
    odh::BoneMotion motion{};motion.frames=frames;motion.time_properties={{{0,{{"xevt","0"}}}}};
    value->motions.push_back(motion);return value;
}
struct Fixture {
    odh::World world;
    std::array<std::shared_ptr<odh::AnimationHeader>,4> clips{{clip(),clip(),clip(),clip()}};
    std::shared_ptr<odh::AnimationHeader> carry=clip(3),other=clip(8);
    float height=0;bool missing_slot=false,missing_clip=false,refuse=false;
    unsigned transfers=0;std::uint16_t selected=99;
    bool probe_reentry=false,reentry_rejected=false,cancel_rejected=false;
    odh::VmPrimitive override;
    odh::Playback playback{world,[&](const auto&,auto,auto){return other;},{},[&](const auto&,auto){return carry;}};
    odh::VmPrimitive reach;
    odh::Vm vm;
    explicit Fixture(std::uint16_t mode=0,bool populate=true):vm(program(mode),world.vm_memory(),{},
        [&](const auto& f,const auto& i,const auto& t,const auto& d){return override?override(f,i,t,d):reach(f,i,t,d);}){
        if(populate){
            require(world.create(1,odh::EntityKind::avatar,{0,88,3}),"create actor");
            require(world.create(2,odh::EntityKind::object,{0,0,3})&&world.create(3,odh::EntityKind::object),"create container/item");
            for(std::int16_t id=1;id<=3;++id)require(world.set_position(id,{16*id,16,1,0}),"position fixture");
            if(mode==1)require(world.place_in_slot(2,1,3),"source storage item");
        }
        odh::Playback::ReachServices services;
        services.slot_height=[&](std::int16_t,std::size_t)->std::optional<float>{return missing_slot?std::nullopt:std::optional<float>{height};};
        services.animation=[&](std::uint16_t profile)->std::shared_ptr<const odh::AnimationHeader>{selected=profile;return missing_clip?nullptr:clips.at(profile);};
        services.transfer=[&](const auto& f,const auto& i,const auto& d,std::int16_t item,std::int16_t container,
                std::size_t slot,std::string& error)->std::optional<bool>{
            if(probe_reentry){
                reentry_rejected=reach(f,i,vm.temps(),d).disposition==odh::PrimitiveDisposition::unsupported;
                try{playback.cancel();}catch(const std::logic_error&){cancel_rejected=true;}
            }
            ++transfers;require(i.opcode==47&&d.origin==&vm&&vm.matches_active_primitive(f,i,d),"transfer authentic47");
            if(refuse)return false;
            auto move=world.prepare_move_to_slot(container,slot,item);
            if(!move){error="unsupported World move";return {};}
            auto plan=playback.prepare_slot_carry_change(*move,f,vm,i,d,1,world.find(1)->generation,vm.run_generation(),&error);
            if(!plan)return {};
            require(plan->can_commit()&&!plan->can_publish(),"carry preflight before actual move");
            require(world.commit_move_to_slot(*move)&&plan->can_publish()&&plan->publish()&&!plan->publish(),"exact World move publishes carry once");
            return true;
        };
        reach=playback.reach_primitive(std::move(services));require(playback.bind(vm),"bind Reach playback");
    }
    void start(std::uint16_t mode=0){vm.start(key,{1,1,static_cast<std::int16_t>(mode==1?2:3)},{1});}
    odh::PlaybackSnapshot snapshot(){odh::PlaybackSnapshot value;std::string why;
        require(playback.capture_snapshot(value,&why),why.c_str());return value;}
    void restore(const Fixture& source,const odh::PlaybackSnapshot& value){
        clips=source.clips;carry=source.carry;other=source.other;height=source.height;
        require(world.restore_snapshot(source.world.snapshot())&&vm.restore_snapshot(source.vm.snapshot()),"fresh World/VM restore");
        std::string why;require(playback.restore_snapshot(value,vm,[&](const auto& k)->std::shared_ptr<const odh::AnimationHeader>{
            if(k.kind==odh::PlaybackResourceKey::Kind::reach)return clips.at(k.id);
            if(k.kind==odh::PlaybackResourceKey::Kind::posture_reset&&k.posture==-1)return carry;
            if(k.kind==odh::PlaybackResourceKey::Kind::animation)return other;
            return {};
        },&why),why.c_str());
    }
};
void floor_carry_clear_test(){
    odh::World world;require(world.create(1,odh::EntityKind::avatar,{0,88,3})&&
        world.create(3,odh::EntityKind::object),"floor carry create");
    require(world.set_position(1,{16,16,1,0})&&world.set_position(3,{16,16,1,0})&&world.place_in_slot(1,0,3),"floor carry source hand");
    const auto animation=clip();odh::Playback playback(world,[&](const auto&,auto,auto){return animation;});
    odh::Vm* running=nullptr;unsigned publications=0;
    const odh::EntityPosition destination{32,16,1,2};
    const odh::Bhav routine{0x8002,{{44,1,255,{1,0,0,0,0,16,0,0}},{5,254,255,{}}},{},odh::Bhav::Metadata{0,2,0,0}};
    odh::Vm vm({{key,routine}},world.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d)->odh::PrimitiveOutcome{
        if(i.opcode==44)return playback.primitive()(f,i,t,d);
        require(bool(playback.carry_clip()),"real earlier opcode44 installed carry");
        auto forged=i;forged.opcode=47;std::string error;
        require(!playback.prepare_floor_carry_clear(f,*running,forged,d,3,world.find(3)->generation,destination,
            1,world.find(1)->generation,running->run_generation(),&error),"floor carry rejects rewritten instruction proof");
        auto plan=playback.prepare_floor_carry_clear(f,*running,i,d,3,world.find(3)->generation,destination,
            1,world.find(1)->generation,running->run_generation(),&error);
        require(plan&&plan->can_commit()&&!plan->can_publish(),"floor clear prepared before actual source Drop5 move");
        require(world.place_on_floor(3,destination)&&plan->can_publish()&&plan->publish()&&!plan->publish(),
            "exact floor placement permits one carry publication");
        ++publications;return {odh::PrimitiveDisposition::goto_true,{}};
    });
    running=&vm;require(playback.bind(vm),"floor carry binds actual VM");vm.start(key,{1,1,3});
    require(vm.run(40)==odh::VmStatus::returned_true&&publications==1&&!playback.carry_clip()&&
        world.find(1)->slots[0]==0&&!world.find(3)->attachment&&world.find(3)->position==destination,
        "authentic Drop5 clears source avatar carry after reciprocal floor move");
}
void run_to_end(Fixture& f,unsigned first){
    for(unsigned tick=first;tick<20&&f.vm.status()==odh::VmStatus::next_tick;++tick){
        require(f.playback.advance(tick),"advance actual avatar animation phase");
        require(f.vm.resume_next_tick(),"resume retained Reach instruction");f.vm.run(40);
    }
    require(f.vm.status()==odh::VmStatus::returned_true&&!f.playback.progress(),"Reach end clears primary and returns TRUE");
}
}
int main(){try{
    floor_carry_clear_test();
    for(const auto test:std::array<std::pair<float,unsigned>,6>{{{0,1},{1.5f,2},{2.5f,2},{3.5f,3},{4,3},{7,3}}}){
        Fixture f(1);f.height=test.first;f.start(1);
        require(f.vm.run(40)==odh::VmStatus::next_tick&&f.vm.stack().back().pc==0&&!f.vm.pending(),"Reach waits at same original PC without pending44");
        require(f.selected==test.second&&f.snapshot().state->speed==1.0f,"source height ties-to-even and default Reach speed");
    }
    {Fixture f;require(f.world.place_in_slot(2,0,1),"seat actor");f.start();f.vm.run(40);require(f.selected==0,"seated reach profile");}
    {Fixture f(1);f.missing_slot=true;f.start(1);require(f.vm.run(40)==odh::VmStatus::returned_false&&!f.playback.progress(),"absent SLOT returns false without animation");}
    {Fixture f;f.missing_clip=true;f.start();require(f.vm.run(40)==odh::VmStatus::returned_false,"absent animation returns authored false");}
    {Fixture f(2);f.start();require(f.vm.run(40)==odh::VmStatus::fault,"source mouth mode unsupported");}
    for(const auto mode:std::array<std::uint16_t,2>{{0,1}}){
        Fixture source(mode);source.start(mode);require(source.vm.run(40)==odh::VmStatus::next_tick,"start Reach");
        const auto before=source.snapshot();Fixture resumed(mode,false);resumed.restore(source,before);
        require(odh::encode_vm_snapshot(source.vm.snapshot())==odh::encode_vm_snapshot(resumed.vm.snapshot()),"fresh same-PC VM byte equality");
        require(source.playback.advance(1)&&resumed.playback.advance(1),"queue source xevt0 in both worlds");
        require(source.vm.resume_next_tick()&&resumed.vm.resume_next_tick(),"release real next tick");
        require(source.vm.run(40)==odh::VmStatus::next_tick&&resumed.vm.run(40)==odh::VmStatus::next_tick,"transfer then retry next tick");
        require(source.transfers==1&&resumed.transfers==1&&source.world.find(1)->slots[0]==3&&source.playback.carry_clip()==source.carry,
            "event0 moves item to hand and installs real default carry once");
        require(odh::encode_world_snapshot(source.world.snapshot())==odh::encode_world_snapshot(resumed.world.snapshot()),
            "restored transfer has exact World bytes including carry attachment");
        const auto after=source.snapshot();Fixture resumed_after(mode,false);resumed_after.restore(source,after);
        run_to_end(source,2);run_to_end(resumed_after,2);
        require(source.transfers==1&&resumed_after.transfers==0&&source.world.find(1)->slots[0]==3&&source.playback.carry_clip()==source.carry&&
            odh::encode_vm_snapshot(source.vm.snapshot())==odh::encode_vm_snapshot(resumed_after.vm.snapshot()),"post-transfer fresh resume keeps item/carry and never repeats pickup");
    }
    {Fixture f(1);require(f.world.move_to_slot(1,0,3),"initial holding for drop");f.start(1);f.vm.run(40);f.playback.advance(1);f.vm.resume_next_tick();f.vm.run(40);
        require(f.world.find(1)->slots[0]==0&&f.world.find(2)->slots[1]==3&&!f.playback.carry_clip(),"mode1 event drops to requested storage and clears carry");}
    {Fixture f;f.probe_reentry=true;f.start();f.vm.run(40);f.playback.advance(1);f.vm.resume_next_tick();
        require(f.vm.run(40)==odh::VmStatus::next_tick&&f.reentry_rejected&&f.cancel_rejected&&f.world.find(1)->slots[0]==3,
            "transfer callback cannot reenter Reach or cancel its primary queue");}
    {Fixture original;auto world=original.world.snapshot();world.entities.front().definition=odh::EntityType{0x1234,{}};
        saved_avatar_fixture_profile(world);
        world.word_count-=world.entities.front().slots.size();world.entities.front().slots.clear();
        Fixture f(0,false);require(f.world.restore_snapshot(world),"import raw zero-slot avatar with known logical hand slot");
        f.start();f.vm.run(40);f.playback.advance(1);f.vm.resume_next_tick();
        require(f.vm.run(40)==odh::VmStatus::next_tick&&f.world.find(1)->slots[0]==3,
            "Reach uses legacy avatar logical hand and grows physical slots only on actual pickup");}
    {Fixture f;f.refuse=true;f.start();f.vm.run(40);f.playback.advance(1);f.vm.resume_next_tick();
        require(f.vm.run(40)==odh::VmStatus::next_tick&&f.transfers==1&&f.snapshot().state->queue.empty()&&f.world.find(1)->slots[0]==0,
            "known placement refusal consumes exactly one event and continues");}
    {Fixture f;f.start();f.vm.run(40);auto saved=f.snapshot();saved.state->end=true;saved.state->frame=static_cast<float>(saved.state->frames);saved.state->queue={0};
        Fixture copy(0,false);copy.restore(f,saved);copy.vm.resume_next_tick();
        require(copy.vm.run(40)==odh::VmStatus::returned_true&&copy.transfers==0,"end takes precedence over queued transfer event");}
    {Fixture f;require(f.world.place_in_slot(1,1,3),"completed pickup in nonhand avatar slot");f.start();f.vm.run(40);auto saved=f.snapshot();
        saved.state->resource.kind=odh::PlaybackResourceKey::Kind::animation;saved.state->resource.id=99;saved.state->speed=1.2f;
        saved.state->queue.clear();Fixture copy(0,false);copy.restore(f,saved);copy.vm.resume_next_tick();
        require(copy.vm.run(40)==odh::VmStatus::next_tick&&copy.snapshot().state->resource.kind==odh::PlaybackResourceKey::Kind::animation,
            "completedPickup preserves another current animation for any caller slot");}
    {Fixture f;f.start();f.vm.run(40);auto saved=f.snapshot();saved.state->resource.id=4;Fixture copy(0,false);
        require(copy.world.restore_snapshot(f.world.snapshot())&&copy.vm.restore_snapshot(f.vm.snapshot()),"negative restore setup");std::string why;
        require(!copy.playback.restore_snapshot(saved,copy.vm,[](const auto&){return clip();},&why),"invalid Reach profile rejected on restore");}
    std::cout<<checks<<" Reach47 checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
