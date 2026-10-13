#include "odh/vm.hpp"
#include <iostream>

namespace {
int checks=0;
void require(bool v,const char* message) {++checks;if(!v)throw std::runtime_error(message);}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions) {
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,1,0}};
}
}
int main() {
    try {
        const auto key=odh::routine_key(4096),child=odh::routine_key(4097);
        int dispatches=0;
        odh::Vm vm({{key,routine({{4097,254,255,{}}})},
                    {child,routine({{45,1,255,{1,0,2,0,0,0,0,0}},{44,254,255,{}}})}}, {}, {},
            [&](const auto& f,const auto& i,const auto& temps) {
                ++dispatches;
                require(f.context.caller==1 && f.context.stack_object==2,"service sees saved entity context");
                require(temps[0]==17,"service sees thread registers");
                return odh::PrimitiveOutcome{i.opcode==45 ? odh::PrimitiveDisposition::wait : odh::PrimitiveDisposition::goto_true,{}};
            });
        vm.start(key,{1,2,2});vm.temps()[0]=17;
        require(vm.run(100)==odh::VmStatus::waiting && dispatches==1,"route service suspends nested routine");
        require(vm.stack().size()==2 && vm.stack().back().pc==0 && vm.steps()==2,"waiting retains call stack/PC");
        const auto first=vm.pending()->ticket;
        require(vm.pending()->instruction.instruction.opcode==45 && vm.pending()->context.callee==2,"pending request provenance");
        require(vm.run(100)==odh::VmStatus::waiting && vm.steps()==2 && dispatches==1,"waiting does not reissue service");
        require(!vm.complete(first+1,true) && vm.pending()->ticket==first,"wrong ticket leaves state unchanged");
        require(vm.complete(first,true) && vm.status()==odh::VmStatus::running && vm.stack().back().pc==1,"completion advances original true branch");
        require(!vm.complete(first,false),"duplicate completion rejected");
        require(vm.run(1)==odh::VmStatus::returned_true && dispatches==2 && vm.steps()==3,"resume executes next primitive and unwinds");
        vm.start(key,{1,2,2});vm.temps()[0]=17;vm.run(100);
        const auto second=vm.pending()->ticket;
        require(second>first && !vm.complete(first,true),"tickets survive restart to reject old work");
        require(vm.complete(second,false) && vm.status()==odh::VmStatus::returned_false,"false completion unwinds nested caller");
        vm.start(key,{1,2,2});vm.temps()[0]=17;vm.run(100);
        const auto canceled=vm.pending()->ticket;
        vm.start(key,{1,2,2});
        require(!vm.pending() && !vm.complete(canceled,true) && vm.stack().size()==1,"start invalidates pending without corrupting new frame");
        odh::Vm absent({{key,routine({{45,254,255,{}}})}});absent.start(key);
        require(absent.run(1)==odh::VmStatus::fault && !absent.pending(),"absent service still faults");
        odh::Vm unsupported({{key,routine({{45,254,255,{}}})}}, {}, {},
            [](const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"routing geometry not supplied"};});
        unsupported.start(key);
        require(unsupported.run(1)==odh::VmStatus::fault && unsupported.diagnostic()=="routing geometry not supplied","service diagnostic retained");
        odh::Vm error_return({{key,routine({{45,253,253,{}}})}}, {}, {},
            [](const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
        error_return.start(key);error_return.run(1);
        require(error_return.complete(error_return.pending()->ticket,true) && error_return.status()==odh::VmStatus::fault,"FD fault on completion remains explicit");
        bool guarded=false;
        odh::Vm* ptr=nullptr;
        odh::Vm reentry({{key,routine({{45,254,255,{}}})}}, {}, {},
            [&](const auto&,const auto&,const auto&){
                try{ptr->complete(1,true);}catch(const std::logic_error&){guarded=true;}
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
            });
        ptr=&reentry;reentry.start(key);
        require(reentry.run(1)==odh::VmStatus::returned_false && guarded,"service cannot complete while dispatching");
        std::cout<<checks<<" service checks passed (synthetic fixtures only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Service test failed: "<<e.what()<<'\n';return 1;}
}
