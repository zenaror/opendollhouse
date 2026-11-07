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
    {
        odh::World live;require(live.create(1,odh::EntityKind::avatar)&&live.create(2,odh::EntityKind::object),"authenticated notification participants");
        unsigned calls=0;bool mutate=false;std::optional<odh::VmFrame> retained_frame;
        std::optional<odh::VmDispatch> retained_dispatch;
        auto backend=odh::notify_idle_primitive(live,[&](auto id){
            ++calls;require(id==2,"exact live notification reaches its registered target");
            if(mutate)require(live.remove(2)&&live.create(2,odh::EntityKind::object),"callback replaces numeric target identity");
            return true;
        },{},true);
        odh::VmPrimitive dispatch=[&](const auto& actual,const auto& op,const auto& temps,const auto& proof){
            retained_frame=actual;retained_dispatch=proof;return backend(actual,op,temps,proof);
        };
        const odh::RoutineSet program{{key,{0x8002,{instruction},{},odh::Bhav::Metadata{0,4,0,0}}}};
        odh::Vm actual(program,live.vm_memory(),{},dispatch);
        require(actual.start(key,{1,1,2})==odh::VmStatus::running&&actual.run(1)==odh::VmStatus::returned_true&&calls==1,
            "ordinary opcode49 authenticates the actual VM source and exact target");
        require(backend(*retained_frame,instruction,{},*retained_dispatch).disposition==odh::PrimitiveDisposition::unsupported&&calls==1,
            "retained dispatch cannot repeat an expired notification");
        require(actual.start(key,{1,1,2})==odh::VmStatus::running,"fresh synchronous notification query starts");
        const auto check=actual.run_synchronous_check(10,dispatch);
        require(check.status==odh::VmStatus::returned_true&&calls==2&&retained_dispatch->check_mode,
            "actual synchronous check may notify a real registered thread without a fabricated primitive frame");
        require(actual.start(key,{1,1,2})==odh::VmStatus::running,"retain exact StackObject before deletion");
        const auto retained_context=actual.stack().back().context;
        const auto old=live.find(2)->generation;
        require(live.remove_retaining_body({2,old})&&live.create(2,odh::EntityKind::object),"target body survives immediate numeric ID reuse");
        require(actual.run(1)==odh::VmStatus::fault&&calls==2&&actual.diagnostic().find("thread availability is unknown")!=std::string::npos,
            "retained old body never notifies its replacement or invents a null Thread");
        require(live.collect_retained_bodies({}),"fixture deliberately drops an unrooted retained body");
        require(actual.start(key,retained_context)==odh::VmStatus::running&&actual.run(1)==odh::VmStatus::fault&&calls==2&&
            actual.diagnostic().find("strong StackObject body is unavailable")!=std::string::npos,
            "missing strong body after collection is unsupported rather than a known nil reference");
        require(live.remove(2)&&actual.start(key,{1,1,2})==odh::VmStatus::running&&live.create(2,odh::EntityKind::object),
            "fresh numeric lookup captures known nil before a replacement appears");
        require(actual.run(1)==odh::VmStatus::returned_true&&calls==2,"known-nil positive StackObject ID never notifies a later instance");
        mutate=true;require(actual.start(key,{1,1,2})==odh::VmStatus::running,"prepare callback identity invalidation");
        require(actual.run(1)==odh::VmStatus::fault&&calls==3&&actual.diagnostic().find("participant changed")!=std::string::npos,
            "callback identity change rejects completion while retaining the real callback effects");
    }
    std::cout<<checks<<" idle notification checks passed (synthetic backend only)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
