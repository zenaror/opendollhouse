#include "odh/playback.hpp"
#include <iostream>

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
    std::shared_ptr<odh::AnimationHeader> a=clip();
    std::uint32_t source=0;std::uint16_t selected=0;
    odh::Playback player{world,[&](const auto&,auto scope,auto id){source=scope;selected=id;return a;}};
    odh::Vm vm;
    Fixture(std::uint8_t flags=0,std::uint8_t expected=0,std::uint16_t id=1,odh::PlaybackLimits limits={}):
        player{world,[&](const auto&,auto scope,auto selected_id){source=scope;selected=selected_id;return a;},limits},
        vm({{odh::routine_key(4096),routine(flags,expected,id)}},world.vm_memory(),{},player.primitive()){
        world.create(1,odh::EntityKind::avatar,{0,88,0});world.declare_object_word(1,17,0);player.bind(vm);
    }
    void start(const std::vector<std::int16_t>& args={}){vm.start(odh::routine_key(4096),{1,2,2},args);vm.run(20);}
    void xevt(std::int32_t time,const std::string& value){a->motions[0].time_properties={{{time,{{"xevt",value}}}}};}
};
}
int main(){try{
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
    for(const auto flags:{1,16,17,128}){Fixture f(static_cast<std::uint8_t>(flags));f.start();
        require(f.vm.status()==odh::VmStatus::fault&&!f.player.progress(),"unsupported loop/carry/mode/flag faults");}
    {Fixture f(0,0,0);f.start();require(f.vm.status()==odh::VmStatus::fault,"reset not falsely completed without posture resources");}
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
    std::cout<<checks<<" playback checks passed (silent synthetic resources only)\n";return 0;
}catch(const std::exception& e){std::cerr<<"Playback test failed: "<<e.what()<<'\n';return 1;}}
