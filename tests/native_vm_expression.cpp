#include "odh/vm.hpp"
#include "odh/vm_snapshot_codec.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int checks=0;
void require(bool value,const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
odh::RoutineKey key() { return odh::routine_key(4096); }
odh::Instruction expr(unsigned op,int lhs,int rhs,unsigned ls=8,unsigned rs=7,
                      unsigned yes=254,unsigned no=255) {
    const auto l=static_cast<std::uint16_t>(lhs),r=static_cast<std::uint16_t>(rhs);
    return {2,static_cast<std::uint8_t>(yes),static_cast<std::uint8_t>(no),
        {static_cast<std::uint8_t>(l),static_cast<std::uint8_t>(l>>8),
         static_cast<std::uint8_t>(r),static_cast<std::uint8_t>(r>>8),
         0xA5,static_cast<std::uint8_t>(op),static_cast<std::uint8_t>(ls),static_cast<std::uint8_t>(rs)}};
}
odh::RoutineSet program(std::initializer_list<odh::Instruction> code) {
    return {{key(),{0x8002,code,{},odh::Bhav::Metadata{0,4,4,0}}}};
}
bool truth(odh::VmStatus status) { return status==odh::VmStatus::returned_true; }
struct Case {int op,lhs,rhs,value;bool result;};
void operators() {
    // Every operator enters through a BHAV instruction and the real Vm branch.
    const Case cases[]={{0,7,3,7,true},{1,7,3,7,false},{2,7,7,7,true},
        {3,32767,1,-32768,true},{4,-32768,1,32767,true},{5,9,-37,-37,true},
        {6,300,300,24464,true},{7,-7,2,-3,true},{8,-32768,16,-32768,true},
        {9,0,16,-32768,true},{10,-1,16,32767,true},{11,7,9,8,true},
        {12,-7,3,2,true},{13,7,3,3,true},{14,7,7,7,true},{15,8,7,8,false},
        {16,7,8,7,true},{17,7,5,6,true},{18,4,2,6,true},{19,7,3,4,true},
        {20,0,81,9,true},{7,17,0,-1,true},{12,-17,0,-17,true},
        {20,17,-1,0,true},{9,0,33,1,true},{8,-1,0,-1,false}};
    for (const auto& c:cases) {
        odh::Vm vm(program({expr(static_cast<unsigned>(c.op),0,c.rhs)}));
        require(vm.start(key())==odh::VmStatus::running,"operator starts");
        vm.temps()[0]=static_cast<std::int16_t>(c.lhs);
        require(vm.run(1)==(c.result?odh::VmStatus::returned_true:odh::VmStatus::returned_false),
                "shared expression operator selects actual VM branch");
        require(vm.temps()[0]==c.value&&vm.steps()==1&&vm.stack().empty(),
                "operator publishes expected register and unwinds frame");
    }
}
void wide_registers() {
    odh::Vm vm(program({expr(3,0,1,42,7,1),expr(5,19,0,8,42,2),
                       expr(20,1,0,42,42,3),expr(2,1,0,42,7)}));
    vm.start(key());vm.temp_xl()[0]=std::numeric_limits<std::int32_t>::max();
    require(vm.run(1)==odh::VmStatus::budget_exhausted&&
            vm.temp_xl()[0]==std::numeric_limits<std::int32_t>::min(),"TempXL wraps in 32 bits");
    require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.temps()[19]==0,
            "copying TempXL to Temp truncates to low16");
    require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.temp_xl()[1]==0,
            "negative TempXL sqrt writes zero");
    require(vm.run(1)==odh::VmStatus::returned_true,"wide register instruction stream finishes");
    for (const auto op:{7,12}) {
        odh::Vm over(program({expr(static_cast<unsigned>(op),0,-1,42)}));
        over.start(key());over.temp_xl()[0]=std::numeric_limits<std::int32_t>::min();
        require(over.run(1)==odh::VmStatus::fault&&over.stack().front().pc==0&&
                over.temp_xl()[0]==std::numeric_limits<std::int32_t>::min(),
                "int32 division/remainder overflow faults without undefined behavior");
    }
    odh::Vm sqrt(program({expr(20,1,0,42,42)}));sqrt.start(key());
    sqrt.temp_xl()[0]=std::numeric_limits<std::int32_t>::max();
    require(sqrt.run(1)==odh::VmStatus::returned_true&&sqrt.temp_xl()[1]==-19196,
            "sqrt casts to signed short before storing in TempXL");
    odh::Vm flag(program({expr(9,0,32,42,7,1),expr(8,0,32,42)}));flag.start(key());
    require(flag.run(2)==odh::VmStatus::returned_false&&
            flag.temp_xl()[0]==std::numeric_limits<std::int32_t>::min(),
            "bit31 set stores full width and signed flag query remains false");
    for (const auto instruction:{expr(2,-1,0,42),expr(2,2,0,42),expr(5,2,1,42),
                                expr(2,-1,0),expr(2,20,0),expr(5,20,1)}) {
        odh::Vm invalid(program({instruction}));invalid.start(key());
        require(invalid.run(1)==odh::VmStatus::fault,"Temp/TempXL invalid bounds fault");
    }
}
void indirect_and_effects() {
    odh::Vm vm(program({expr(11,0,0,11,11)}));vm.start(key());
    vm.temps()[0]=19;vm.temps()[19]=4;
    require(vm.run(1)==odh::VmStatus::returned_false&&vm.temps()[19]==5&&vm.temps()[0]==19,
            "TempByTemp uses same-bank alias and RHS sees increment");
    odh::Vm bad(program({expr(5,0,7,11)}));bad.start(key());bad.temps()[0]=20;
    require(bad.run(1)==odh::VmStatus::fault&&bad.temps()[0]==20,"indirect destination bounds fault");
    odh::Vm write_before_fault(program({expr(11,19,0,8,16)}));write_before_fault.start(key());
    write_before_fault.temps()[19]=4;
    require(write_before_fault.run(1)==odh::VmStatus::fault&&write_before_fault.temps()[19]==5&&
            write_before_fault.stack().front().pc==0,"RHS read fault preserves prior increment write");
    odh::VmMemory memory;
    memory.read=[](const auto&,std::uint8_t,std::int16_t)->std::optional<std::int16_t>{
        throw std::runtime_error("host read exception");
    };
    odh::Vm throws(program({expr(11,0,0,8,16)}),memory);throws.start(key());
    bool caught=false;
    try { throws.run(1); } catch (const std::runtime_error& error) {
        caught=std::string(error.what())=="host read exception";
    }
    require(caught&&throws.temps()[0]==1&&throws.stack().front().pc==0,
            "host exception is rethrown outside ABI with prior write preserved");
    memory={};memory.expression_write=[](const auto&,auto,auto,auto)->std::optional<bool> {
        throw std::runtime_error("host write exception");
    };
    odh::Vm write_throws(program({expr(5,0,7,6)}),memory);write_throws.start(key());caught=false;
    try { write_throws.run(1); } catch (const std::runtime_error& error) {
        caught=std::string(error.what())=="host write exception";
    }
    require(caught&&write_throws.stack().front().pc==0,"setter exception is rethrown outside ABI");
    memory={};memory.write=[](const auto&,auto,auto,auto){return false;};
    odh::Vm unsupported(program({expr(5,0,7,6)}),memory);unsupported.start(key());
    require(unsupported.run(1)==odh::VmStatus::fault,"legacy false setter remains unsupported");
    memory.expression_write=[](const auto&,auto,auto,auto)->std::optional<bool>{return false;};
    memory.read=[](const auto&,auto,auto)->std::optional<std::int16_t>{return 5;};
    for (const auto op:{5,9,10,20,3,11}) {
        odh::Vm rejected(program({expr(static_cast<unsigned>(op),0,7,6)}),memory);rejected.start(key());
        require(rejected.run(1)==(op==3||op==11?odh::VmStatus::returned_true:odh::VmStatus::returned_false),
                "known rejected setter observed only by reference operators");
    }
    for (const auto scope:{7,21,26,35,38}) {
        odh::Vm readonly(program({expr(5,0,7,static_cast<unsigned>(scope))}));readonly.start(key());
        require(readonly.run(1)==odh::VmStatus::returned_false,"pinned read-only setter returns false");
    }
}
void lazy_levels() {
    std::string calls;
    odh::VmMemory memory;
    memory.read=[&](const auto&,auto,auto)->std::optional<std::int16_t>{calls+='R';return 2000;};
    memory.expression_levels=[&](const odh::VmFrame& frame)->std::optional<std::array<std::int32_t,3>> {
        require(frame.context.caller==11&&frame.context.callee==22,"levels receives active frame");
        calls+='L';return std::array<std::int32_t,3>{3,5,2};
    };
    for (const auto op:{1,14}) {
        calls.clear();odh::Vm vm(program({expr(static_cast<unsigned>(op),29,1024,3)}),memory);
        vm.start(key(),{11,22,33});
        require(truth(vm.run(1))==(op==1)&&calls=="RL","exact room comparison reads before lazy levels");
    }
    for (const auto instruction:{expr(0,29,1024,3),expr(1,28,1024,3),expr(1,29,1023,3),expr(1,29,1024,4)}) {
        calls.clear();odh::Vm vm(program({instruction}),memory);vm.start(key());vm.run(1);
        require(calls=="R","unrelated comparisons never request levels");
    }
    memory.expression_levels={};odh::Vm absent(program({expr(1,29,1024,3)}),memory);absent.start(key());
    require(absent.run(1)==odh::VmStatus::fault&&absent.diagnostic()=="unsupported expression room levels",
            "missing exact room adapter faults explicitly");
}
void snapshot_continuation() {
    const auto code=program({expr(3,0,7,42,7,1),expr(5,19,0,8,42,2),
                             expr(11,0,1,11,42,3,3),expr(5,1,19,42,8)});
    odh::Vm first(code);first.start(key(),{11,22,33});first.temp_xl()={70000,100};
    first.temps()[0]=19;first.temps()[19]=-5;
    require(first.run(1)==odh::VmStatus::budget_exhausted,"snapshot taken at real instruction boundary");
    const auto saved=first.snapshot();
    const auto decoded=odh::decode_vm_snapshot(odh::encode_vm_snapshot(saved));
    odh::Vm resumed(code);require(resumed.restore_snapshot(decoded),"snapshot restores existing native VM");
    require(resumed.temp_xl()[0]==70007&&resumed.stack().front().pc==1&&
            resumed.temps()[19]==-5,"snapshot retains full XL, Temp and instruction pointer");
    require(first.run(10)==odh::VmStatus::returned_true&&resumed.run(10)==odh::VmStatus::returned_true,
            "original and restored native VM finish");
    require(first.temps()==resumed.temps()&&first.temp_xl()==resumed.temp_xl()&&
            first.temp_xl()[1]==4472&&first.steps()==resumed.steps(),
            "snapshot continuation preserves aliased execution effects");
}
}
int main() {
    try {operators();wide_registers();indirect_and_effects();lazy_levels();snapshot_continuation();
        std::cout<<checks<<" native VM expression checks passed\n";
    } catch (const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
