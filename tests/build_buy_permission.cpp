#include "odh/build_buy_permission.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
odh::RoutineKey key(){return odh::routine_key(4096);}
}
int main(){try{
    odh::BuildBuyPermission state;std::vector<bool> events;
    require(state.enabled(),"new source architecture enables Build/Buy");
    const auto handler=odh::build_buy_permission_primitive(state,[&](bool value){
        require(state.enabled()==value,"event observes the already-published permission state");events.push_back(value);});
    odh::RoutineSet code{{key(),{0x8002,{{1,1,255,{10,0,0,0,0,0,0,0}},
        {1,2,255,{10,0,0,0,0,0,0,0}},{1,254,255,{11,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,0,0,0}}}};
    odh::Vm vm(code,{}, {},handler);
    vm.start(key(),{}, {},true);vm.run(1);
    require(!state.enabled()&&events==std::vector<bool>{false},"Generic10 immediately disables the shared authority");
    const auto saved=odh::encode_build_buy_permission(state.snapshot());
    odh::BuildBuyPermission reopened;reopened.restore(odh::decode_build_buy_permission(saved));
    require(!reopened.enabled()&&events.size()==1,"restore preserves false without inventing a VM notification");
    vm.run(10);
    require(vm.status()==odh::VmStatus::returned_true&&state.enabled()&&events==std::vector<bool>({false,false,true}),
        "repeated Generic10 still emits source event and Generic11 enables the same authority");
    odh::Vm check({{key(),{0x8002,{{1,254,255,{10,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,0,0,0}}}});
    check.start(key(),{}, {},true);const auto result=check.run_synchronous_check(10,handler);
    require(result.status==odh::VmStatus::returned_true&&!state.enabled(),"source check uses the same mutable permission authority");
    require(!state.apply_generic(9)&&!state.enabled(),"unrelated generic modes do not change permission");
    odh::VmFrame frame;odh::Instruction instruction{1,254,255,{11,0,0,0,0,0,0,0}};
    require(handler(frame,instruction,{},odh::VmDispatch{&vm,1,false}).disposition==odh::PrimitiveDisposition::unsupported&&!state.enabled(),
        "forged/stale dispatch cannot change permission");
    for(const auto bytes:std::vector<std::vector<std::uint8_t>>{{},{1,0},{1,0,2},{2,0,0},{1,0,0,0}}){
        bool rejected=false;try{(void)odh::decode_build_buy_permission(bytes);}catch(const odh::FormatError&){rejected=true;}
        require(rejected,"permission codec rejects malformed/version/trailing/nonboolean data");
    }
    std::cout<<"Build/Buy shared permission checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
