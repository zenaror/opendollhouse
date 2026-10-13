#include "odh/notify.hpp"
#include <iostream>
namespace {int checks=0;void require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}}
int main(){try{
    odh::World world;world.create(2,odh::EntityKind::object);
    const odh::VmFrame frame{odh::routine_key(4096),0,{1,2,2},{},{}};
    const odh::Instruction instruction{49,254,255,{255,255,255,255,255,255,255,255}};
    std::string status;int submissions=0;
    auto observe=[&](std::int16_t id,const std::string& s){require(id==2,"notification retains stack object ID");status=s;};
    auto invoke=[&](const odh::VmPrimitive& f){return f(frame,instruction,{},{}).disposition;};
    require(invoke(odh::notify_idle_primitive(world,{},observe))==odh::PrimitiveDisposition::goto_true&&status=="no_thread","no thread backend returns true with explicit status");
    auto accepted=odh::notify_idle_primitive(world,[&](auto id){++submissions;return id==2;},observe);
    require(invoke(accepted)==odh::PrimitiveDisposition::goto_true&&status=="notified"&&submissions==1,"registered backend receives interruption");
    require(invoke(odh::notify_idle_primitive(world,[](auto){return false;},observe))==odh::PrimitiveDisposition::goto_true&&status=="no_thread","absent registered thread does not change branch");
    world.remove(2);require(invoke(accepted)==odh::PrimitiveDisposition::goto_true&&status=="missing_object"&&submissions==1,"missing object does not invoke backend");
    const auto key=odh::routine_key(4096);
    odh::Vm vm({{key,{0x8002,{instruction},{},odh::Bhav::Metadata{0,4,0,0}}}}, {},{},accepted);
    vm.start(key,{1,2,2});require(vm.run(1)==odh::VmStatus::returned_true&&!vm.pending(),"notification is synchronous and ignores all operand bytes");
    auto other=instruction;other.opcode=0;require(accepted(frame,other,{},{}).disposition==odh::PrimitiveDisposition::unsupported,"unrelated opcode unsupported");
    std::cout<<checks<<" idle notification checks passed (synthetic backend only)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
