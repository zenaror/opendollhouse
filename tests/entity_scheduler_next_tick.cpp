#include "odh/entity_scheduler.hpp"
#include "odh/entity_scheduler_snapshot_codec.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/formats.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F> bool rejects(F f){try{f();}catch(const odh::FormatError&){return true;}return false;}
const auto key=odh::routine_key(4096);
odh::RoutineSet program(){return {{key,odh::Bhav{0x8002,{
    {18,1,255,{}},{2,254,255,{0,0,77,0,0,5,8,7}}
},{},odh::Bhav::Metadata{0,4,2,0}}}};}
struct Fixture {
    unsigned requests=0;
    odh::Vm vm{program(),{}, {},[this](const auto&,const auto&,const auto&,const auto&){
        ++requests;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true_next_tick,{}};
    }};
    odh::EntityVmScheduler scheduler{2};
    Fixture(){require(scheduler.add(7,vm),"register actual VM before scheduler restore or dispatch");}
};
}
int main(){try{
    Fixture control;control.vm.start(key,{7,7,7},{23});control.vm.temps()[4]=91;
    const auto ordinary=odh::encode_entity_vm_scheduler_snapshot(control.scheduler.snapshot());
    // Frozen version1 representation: limit2, one entity7, generation1,
    // steps0, running2, no hooks/cursors/ticks/fault/diagnostic.
    std::vector<std::uint8_t> golden(51,0);
    golden[0]=1;golden[2]=2;golden[10]=1;golden[14]=7;golden[16]=1;golden[32]=2;
    require(ordinary==golden,"ordinary status retains exact frozen scheduler version1 bytes");
    require(odh::encode_entity_vm_scheduler_snapshot(odh::decode_entity_vm_scheduler_snapshot(ordinary))==ordinary,
        "legacy scheduler snapshot remains byte-stable after decode and encode");
    require(control.scheduler.tick(1,100,100)&&control.vm.status()==odh::VmStatus::next_tick&&
        control.vm.steps()==1&&control.vm.stack()[0].pc==1&&control.vm.temps()[0]==0&&control.requests==1&&
        control.scheduler.instructions_last_tick()==1,"scheduler cannot consume remaining budget after next-tick branch");
    const auto vm_bytes=odh::encode_vm_snapshot(control.vm.snapshot());
    const auto scheduler_bytes=odh::encode_entity_vm_scheduler_snapshot(control.scheduler.snapshot());
    require(scheduler_bytes[0]==2&&scheduler_bytes[1]==0&&scheduler_bytes[32]==9,
        "next-tick scheduler status conditionally selects version2");
    require(!control.scheduler.tick(1,100,100)&&control.vm.status()==odh::VmStatus::next_tick&&
        odh::encode_vm_snapshot(control.vm.snapshot())==vm_bytes&&control.requests==1,
        "duplicate same tick is rejected before scheduler releases next-tick VM");
    require(control.vm.run(100)==odh::VmStatus::next_tick&&control.vm.temps()[0]==0,
        "direct run also cannot bypass required host boundary");
    Fixture restored;
    require(restored.vm.restore_snapshot(odh::decode_vm_snapshot(vm_bytes))&&
        restored.scheduler.restore_snapshot(odh::decode_entity_vm_scheduler_snapshot(scheduler_bytes)),
        "equivalent registered VM and scheduler restore next-tick checkpoint");
    restored.requests=control.requests;
    require(!restored.scheduler.tick(1,100,100)&&restored.vm.status()==odh::VmStatus::next_tick&&
        odh::encode_vm_snapshot(restored.vm.snapshot())==vm_bytes,
        "restored scheduler cannot release continuation in saved tick either");
    require(control.scheduler.tick(2,100,100)&&restored.scheduler.tick(2,100,100)&&
        control.vm.status()==odh::VmStatus::returned_true&&restored.vm.status()==odh::VmStatus::returned_true&&
        control.vm.temps()[0]==77&&control.vm.temps()[4]==91&&control.vm.steps()==2&&
        control.requests==1&&restored.requests==1,"next boundary resumes each continuation once without primitive redispatch");
    require(odh::encode_vm_snapshot(control.vm.snapshot())==odh::encode_vm_snapshot(restored.vm.snapshot())&&
        odh::encode_entity_vm_scheduler_snapshot(control.scheduler.snapshot())==
        odh::encode_entity_vm_scheduler_snapshot(restored.scheduler.snapshot()),
        "continued VM and scheduler are byte-identical to uninterrupted control");
    require(odh::encode_entity_vm_scheduler_snapshot(control.scheduler.snapshot())[0]==1,
        "scheduler returns to legacy wire profile after next-tick state is consumed");
    auto bad=scheduler_bytes;bad[0]=1;
    require(rejects([&]{odh::decode_entity_vm_scheduler_snapshot(bad);}),"version1 cannot carry status9");
    bad=scheduler_bytes;bad[0]=3;
    require(rejects([&]{odh::decode_entity_vm_scheduler_snapshot(bad);}),"unknown scheduler version3 rejected");
    bad=scheduler_bytes;bad[32]=10;
    require(rejects([&]{odh::decode_entity_vm_scheduler_snapshot(bad);}),"unknown status rejected even in version2");
    for(std::size_t n=0;n<scheduler_bytes.size();++n){auto cut=scheduler_bytes;cut.resize(n);
        require(rejects([&]{odh::decode_entity_vm_scheduler_snapshot(cut);}),"truncated next-tick scheduler snapshot rejected");}
    bad=scheduler_bytes;bad.push_back(0);
    require(rejects([&]{odh::decode_entity_vm_scheduler_snapshot(bad);}),"trailing scheduler snapshot bytes rejected");
    std::cout<<checks<<" scheduler next-tick boundary checks passed (native component fixtures)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
