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
            [&](const auto& f,const auto& i,const auto& temps,const auto&) {
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
            [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::unsupported,"routing geometry not supplied"};});
        unsupported.start(key);
        require(unsupported.run(1)==odh::VmStatus::fault && unsupported.diagnostic()=="routing geometry not supplied","service diagnostic retained");
        odh::Vm error_return({{key,routine({{45,253,253,{}}})}}, {}, {},
            [](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
        error_return.start(key);error_return.run(1);
        require(error_return.complete(error_return.pending()->ticket,true) && error_return.status()==odh::VmStatus::fault,"FD fault on completion remains explicit");
        bool guarded=false;
        bool update_guarded=false;
        odh::Vm* ptr=nullptr;
        odh::Vm reentry({{key,routine({{45,254,255,{}}})}}, {}, {},
            [&](const auto&,const auto&,const auto&,const auto&){
                try{ptr->complete(1,true);}catch(const std::logic_error&){guarded=true;}
                try{ptr->update_pending(1,{odh::VmFrameBank::arguments,0,99});}catch(const std::logic_error&){update_guarded=true;}
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_false,{}};
            });
        ptr=&reentry;reentry.start(key);
        require(reentry.run(1)==odh::VmStatus::returned_false && guarded && update_guarded,"service cannot complete or update while dispatching");
        for (const auto bank : {odh::VmFrameBank::arguments,odh::VmFrameBank::locals}) {
            const std::uint8_t scope=bank==odh::VmFrameBank::arguments ? 9 : 25;
            const auto program=routine({{44,1,2,{}},
                {2,254,255,{0,0,42,0,0,2,scope,7}},
                {2,254,255,{0,0,42,0,0,2,scope,7}}});
            for (const bool result : {true,false}) {
                odh::Vm event({{key,program}}, {}, {},
                    [](const auto&,const auto&,const auto&,const auto&) {
                        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
                    });
                require(event.run_generation()==0,"new VM has no start generation");
                event.start(key,{}, {7});
                const auto generation=event.run_generation();
                event.run(1);
                const auto ticket=event.pending()->ticket;
                const auto original=event.stack().back();
                const auto steps=event.steps();
                auto unchanged=[&] {
                    return event.status()==odh::VmStatus::waiting && event.pending() &&
                        event.pending()->ticket==ticket && event.stack().size()==1 &&
                        event.stack().back().pc==original.pc &&
                        event.stack().back().arguments==original.arguments &&
                        event.stack().back().locals==original.locals &&
                        event.steps()==steps && event.diagnostic().empty() &&
                        event.run_generation()==generation;
                };
                require(!event.complete(ticket+1,result,odh::VmFrameWrite{bank,0,42}) && unchanged(),
                        "wrong ticket output leaves waiting frame unchanged");
                const auto size=bank==odh::VmFrameBank::arguments ? original.arguments.size() : original.locals.size();
                require(!event.complete(ticket,result,odh::VmFrameWrite{bank,size,42}) && unchanged(),
                        "out of range output atomically rejects completion");
                require(!event.complete(ticket,result,odh::VmFrameWrite{static_cast<odh::VmFrameBank>(99),0,42}) && unchanged(),
                        "invalid output bank atomically rejects completion");
                require(!event.update_pending(ticket+1,{bank,0,42}) && unchanged(),
                        "wrong ticket update preserves entire waiting state");
                require(!event.update_pending(ticket,{bank,size,42}) && unchanged() &&
                        !event.update_pending(ticket,{static_cast<odh::VmFrameBank>(99),0,42}) && unchanged(),
                        "invalid update bank/index atomically preserves wait");
                require(event.update_pending(ticket,{bank,0,13}) && event.pending()->ticket==ticket &&
                        event.status()==odh::VmStatus::waiting && event.stack().back().pc==0 &&
                        event.steps()==steps && event.run_generation()==generation && event.diagnostic().empty(),
                        "matching pending update publishes without branch or consuming ticket");
                const auto& updated=bank==odh::VmFrameBank::arguments ? event.stack().back().arguments : event.stack().back().locals;
                require(updated[0]==13 && event.update_pending(ticket,{bank,0,17}) && updated[0]==17,
                        "one pending ticket accepts repeated validated updates");
                require(event.complete(ticket,result,odh::VmFrameWrite{bank,0,42}) &&
                        event.stack().back().pc==(result ? 1 : 2) && event.run_generation()==generation,
                        "matching event writes frame and follows selected branch without restarting");
                const auto& values=bank==odh::VmFrameBank::arguments ? event.stack().back().arguments : event.stack().back().locals;
                require(values[0]==42 && !event.complete(ticket,!result,odh::VmFrameWrite{bank,0,99}) && values[0]==42,
                        "duplicate event cannot overwrite delivered value");
                require(event.run(1)==odh::VmStatus::returned_true,"next expression sees delivered argument/local event");
                event.start(key,{}, {7});event.run(1);
                const auto canceled_ticket=event.pending()->ticket;
                event.start(key,{}, {8});
                require(event.run_generation()==generation+2 &&
                        !event.complete(canceled_ticket,true,odh::VmFrameWrite{bank,0,99}) &&
                        event.stack().back().arguments[0]==8 && event.stack().back().locals[0]==0,
                        "restart invalidates event output and identifies new run");
                const auto fresh_generation=event.run_generation();
                require(!event.update_pending(canceled_ticket,{bank,0,99}) && event.stack().back().arguments[0]==8,
                        "restart rejects canceled pending update without new-frame mutation");
                require(event.start(child)==odh::VmStatus::fault && event.run_generation()==fresh_generation+1,
                        "failed start still identifies a new run");
                odh::Vm immediate({{key,program}}, {}, {},
                    [=](const auto&,const auto&,const auto&,const auto&) {
                        return odh::PrimitiveOutcome{result ? odh::PrimitiveDisposition::goto_true : odh::PrimitiveDisposition::goto_false,
                                                     {},odh::VmFrameWrite{bank,0,42}};
                    });
                immediate.start(key);
                require(immediate.run(2)==odh::VmStatus::returned_true,"immediate argument/local output visible on either branch");
            }
            for (const auto disposition : {odh::PrimitiveDisposition::goto_true,odh::PrimitiveDisposition::goto_false,
                                          odh::PrimitiveDisposition::wait,odh::PrimitiveDisposition::unsupported}) {
                odh::Vm invalid({{key,routine({{44,1,1,{}},{44,254,255,{}}})}}, {}, {},
                    [=](const auto&,const auto&,const auto&,const auto&) {
                        return odh::PrimitiveOutcome{disposition,{},odh::VmFrameWrite{bank,4,42}};
                    });
                invalid.start(key,{}, {7});
                require(invalid.run(1)==odh::VmStatus::fault && !invalid.pending() &&
                        invalid.stack().back().pc==0 && invalid.stack().back().arguments[0]==7 &&
                        invalid.stack().back().locals[0]==0 && invalid.diagnostic().find("frame output")!=std::string::npos,
                        "invalid or forbidden output faults without mutation or branch");
            }
        }
        for (const auto disposition : {odh::PrimitiveDisposition::unsupported,
                                      odh::PrimitiveDisposition::goto_true}) {
            odh::Vm invalid({{key,routine({{44,1,1,{}},{44,254,255,{}}})}}, {}, {},
                [=](const auto&,const auto&,const auto&,const auto&) {
                    const auto bank=disposition==odh::PrimitiveDisposition::goto_true ? static_cast<odh::VmFrameBank>(99)
                                                                                    : odh::VmFrameBank::arguments;
                    return odh::PrimitiveOutcome{disposition,{},odh::VmFrameWrite{bank,0,42}};
                });
            invalid.start(key,{}, {7});
            require(invalid.run(1)==odh::VmStatus::fault && !invalid.pending() &&
                    invalid.stack().back().pc==0 && invalid.stack().back().arguments[0]==7 &&
                    invalid.diagnostic().find("frame output")!=std::string::npos,
                    "forbidden valid write or invalid bank never mutates frame");
        }
        for(const auto bank:{odh::VmFrameBank::arguments,odh::VmFrameBank::locals}) {
            odh::Vm wait_write({{key,routine({{44,1,1,{}},{2,254,255,{0,0,42,0,0,2,
                static_cast<std::uint8_t>(bank==odh::VmFrameBank::arguments ? 9 : 25),7}}})}}, {}, {},
                [=](const auto&,const auto&,const auto&,const auto&) {
                    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{},odh::VmFrameWrite{bank,0,42}};
                });
            wait_write.start(key,{}, {7});
            require(wait_write.run(1)==odh::VmStatus::waiting && wait_write.stack().back().pc==0 &&
                    (bank==odh::VmFrameBank::arguments ? wait_write.stack().back().arguments[0] :
                                                       wait_write.stack().back().locals[0])==42,
                    "validated WAIT publishes argument/local before exposing pending instruction");
            require(wait_write.complete(wait_write.pending()->ticket,true) && wait_write.run(1)==odh::VmStatus::returned_true,
                    "following branch observes value published by WAIT");
        }
        std::cout<<checks<<" service checks passed (synthetic fixtures only)\n";return 0;
    }catch(const std::exception& e){std::cerr<<"Service test failed: "<<e.what()<<'\n';return 1;}
}
