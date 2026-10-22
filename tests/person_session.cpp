#include "person_session.hpp"
#include "odh/entity_scheduler.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
odh::RoutineKey root(){return odh::routine_key(4096);}
odh::Bhav routine(std::initializer_list<odh::Instruction> instructions){
    return {0x8002,instructions,{},odh::Bhav::Metadata{0,4,4,0}};
}
}
int main(){try{
    using odh_host::PersonSessionPolicy;
    using odh_host::PersonDiagnostic;
    using odh_host::PersonDiagnosticRetention;
    const PersonSessionPolicy probe{false,false,256},bounded_window{true,true,3},window{true,false,256};
    require(probe.tick_limit()==256&&probe.entity_budget(9999)==1&&probe.entity_budget(10000)==0&&
            probe.limit_reached(256,2)&&probe.limit_reached(1,10000),"finite probe preserves historical budgets");
    require(bounded_window.tick_limit()==3&&!bounded_window.limit_reached(2,40000)&&
            bounded_window.limit_reached(3,40000),"explicit window ticks remain finite while work grants renew");
    require(window.tick_limit()==std::numeric_limits<std::uint32_t>::max()&&
            !window.limit_reached(4096,10000)&&!window.limit_reached(6200,1000000)&&
            window.limit_reached(std::numeric_limits<std::uint32_t>::max(),0),
            "default window has only the existing uint32 controller clock horizon");
    require(window.tick_budget(999999,std::numeric_limits<std::size_t>::max())==640000,
            "aggregate work is bounded and cannot overflow for oversized entity counts");
    std::cout<<"PASS finite probe, explicit ticks, continuous window, aggregate bound\n";

    // Two real VMs run beyond both historical limits without wall-clock pacing.
    // Each wait completes on the next tick; the same run generation is retained.
    std::array<std::uint64_t,3> tickets{};
    const auto wait=[&](const auto& frame,const auto& instruction,const auto&,const odh::VmDispatch& dispatch){
        if(instruction.opcode!=33)return odh::PrimitiveOutcome{};
        tickets.at(static_cast<std::size_t>(frame.context.caller))=dispatch.ticket;
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};
    };
    const auto program=routine({{33,1,255,{}},{2,0,255,{0,0,1,0,0,5,8,7}}});
    odh::Vm one({{root(),program}},{},{},wait),two({{root(),program}},{},{},wait);
    one.start(root(),{1,1,1,0});two.start(root(),{2,2,2,0});
    odh::EntityVmScheduler scheduler(2);
    require(scheduler.add(1,one,[&](auto){return !one.pending()||one.complete(tickets[1],true);})&&
            scheduler.add(2,two,[&](auto){return !two.pending()||two.complete(tickets[2],true);}),
            "long-run VMs registered");
    PersonDiagnosticRetention diagnostics;
    std::vector<odh::VmTrace> trace;
    for(std::uint64_t tick=1;tick<=6200;++tick){
        require(!window.limit_reached(tick,one.steps()+two.steps()),"session remains runnable beyond old ceilings");
        require(scheduler.tick(tick,window.tick_budget(one.steps()+two.steps(),scheduler.size()),
            window.entity_budget(one.steps()+two.steps()),[&](auto,const auto& record){
                if(diagnostics.retain(PersonDiagnostic::trace,trace.size(),10000))trace.push_back(record);
            }),"continuous scheduler accepts next tick after historical instruction total");
        require(scheduler.instructions_last_tick()<=20000,"per-tick aggregate instruction work bounded");
    }
    require(one.steps()==12399&&two.steps()==12399&&one.status()==odh::VmStatus::waiting&&
            two.status()==odh::VmStatus::waiting&&trace.size()==10000&&diagnostics.truncated()&&
            diagnostics.dropped()[static_cast<std::size_t>(PersonDiagnostic::trace)]==14798,
            "long-lived runs retain bounded prefix and exact discarded trace count");
    std::cout<<"PASS 6200 accelerated ticks, two VMs beyond 10k each, trace truncation\n";

    // CPU-bound routines exhaust each slice and resume with fresh independent
    // grants. They cannot consume the other entity's grant or fault the host.
    const auto busy=routine({{2,0,255,{0,0,1,0,0,5,8,7}}});
    odh::Vm busy_one({{root(),busy}}),busy_two({{root(),busy}});
    busy_one.start(root());busy_two.start(root());
    odh::EntityVmScheduler busy_scheduler(2);
    require(busy_scheduler.add(1,busy_one)&&busy_scheduler.add(2,busy_two),"busy VMs registered");
    for(std::uint64_t tick=1;tick<=3;++tick){
        require(busy_scheduler.tick(tick,window.tick_budget(busy_one.steps()+busy_two.steps(),2),
            window.entity_budget(busy_one.steps()+busy_two.steps())),"renewed busy VM grants accepted");
        require(busy_one.steps()==tick*10000&&busy_two.steps()==tick*10000&&
                busy_scheduler.instructions_last_tick()==20000,"each entity receives exactly one bounded tick grant");
    }
    std::cout<<"PASS bounded CPU slices renew independently per entity and tick\n";

    for(std::size_t n=0;n<static_cast<std::size_t>(PersonDiagnostic::count);++n){
        PersonDiagnosticRetention category;
        const auto kind=static_cast<PersonDiagnostic>(n);
        require(category.retain(kind,0,1,8,0,8)&&!category.truncated(),"last fitting record retained");
        require(!category.retain(kind,1,1)&&!category.retain(kind,0,1,9,0,8)&&
                !category.retain(kind,0,1,1,9,8)&&category.dropped()[n]==3,
                "count, bytes and invalid retained-byte overflow reject evidence without throwing");
        std::cout<<"PASS diagnostic category "<<PersonDiagnosticRetention::names[n]<<"\n";
    }
    // Real unsupported opcodes remain faults, even after a diagnostic fills.
    odh::Vm fault({{root(),routine({{99,254,255,{}}})}});
    fault.start(root());odh::EntityVmScheduler fault_scheduler;
    require(fault_scheduler.add(1,fault)&&!fault_scheduler.tick(1,window.tick_budget(0,1),window.entity_budget(0),
        [&](auto,const auto& record){
            if(diagnostics.retain(PersonDiagnostic::trace,trace.size(),10000))trace.push_back(record);
        })&&fault.status()==odh::VmStatus::fault&&!fault_scheduler.diagnostic().empty(),
        "diagnostic saturation does not hide VM faults");
    std::cout<<"PASS VM faults remain faults after retention fills\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
