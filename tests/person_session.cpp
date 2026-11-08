#include "person_session.hpp"
#include "odh/entity_scheduler.hpp"
#include <iostream>
#include <memory>
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
    using odh_host::PersonWorkBudgetPolicy;
    using odh_host::PersonDiagnostic;
    using odh_host::PersonDiagnosticRetention;
    const PersonSessionPolicy probe{false,false,256},bounded_window{true,true,3},window{true,false,256};
    const PersonSessionPolicy extended_probe{false,true,4096,50000};
    require(probe.tick_limit()==256&&probe.entity_budget(9999)==1&&probe.entity_budget(10000)==0&&
            probe.limit_reached(256,2)&&probe.limit_reached(1,10000),"finite probe preserves historical budgets");
    require(probe.probe_instructions==10000&&extended_probe.entity_budget(49999)==1&&
            extended_probe.entity_budget(50000)==0&&extended_probe.instruction_limit_reached(50000)&&
            extended_probe.limit_reached(1,50000),"explicit finite probe budget stops exactly at its configured cap");
    require(bounded_window.tick_limit()==3&&!bounded_window.limit_reached(2,40000)&&
            bounded_window.limit_reached(3,40000),"explicit window ticks remain finite while work grants renew");
    require(window.tick_limit()==std::numeric_limits<std::uint32_t>::max()&&
            !window.limit_reached(4096,10000)&&!window.limit_reached(6200,1000000)&&
            window.limit_reached(std::numeric_limits<std::uint32_t>::max(),0),
            "default window has only the existing uint32 controller clock horizon");
    const PersonSessionPolicy full_day{false,true,30U*60U*24U,10000,PersonWorkBudgetPolicy::per_tick_gameplay};
    require(full_day.tick_limit()==43200&&full_day.tick_limit()<=PersonSessionPolicy::max_headless_ticks&&
            !full_day.limit_reached(43199,100000000)&&full_day.limit_reached(43200,100000000)&&
            full_day.entity_budget(100000000)==PersonSessionPolicy::instructions,
            "continuous day remains finite with the unchanged per-entity tick grant");
    const auto largest_count=std::numeric_limits<std::size_t>::max();
    const auto maximum_work=std::numeric_limits<std::uint64_t>::max();
    const auto largest_funded=maximum_work/PersonSessionPolicy::instructions;
    require(window.tick_budget(999999,largest_count)==
                (largest_count>largest_funded?maximum_work:
                    PersonSessionPolicy::instructions*static_cast<std::uint64_t>(largest_count)),
            "aggregate work saturates without overflow for the host entity-count width");
    require(window.tick_budget(0,0)==0&&window.tick_budget(0,1)==10000&&
            window.tick_budget(0,64)==640000&&window.tick_budget(0,65)==650000,
            "zero and counts around the former64-entity cap retain exact per-entity funding");
    if constexpr(sizeof(std::size_t)>=sizeof(std::uint64_t)){
        require(window.tick_budget(0,static_cast<std::size_t>(largest_funded))==
                    largest_funded*PersonSessionPolicy::instructions&&
                window.tick_budget(0,static_cast<std::size_t>(largest_funded+1))==maximum_work,
                "aggregate multiplication is exact through its last safe count and saturates above it");
    }
    const PersonSessionPolicy window_with_probe_override{true,true,3,50000};
    require(window_with_probe_override.entity_budget(0)==PersonSessionPolicy::instructions&&
            window_with_probe_override.tick_budget(0,2)==20000&&
            !window_with_probe_override.instruction_limit_reached(50000),
            "windowed per-tick grants ignore the finite-probe override");
    std::cout<<"PASS finite probe, explicit ticks, continuous window, aggregate bound\n";

    const PersonSessionPolicy integrated_headless{false,true,3,10000,PersonWorkBudgetPolicy::per_tick_gameplay};
    const PersonSessionPolicy bounded_game_window{true,true,3,10000,PersonWorkBudgetPolicy::per_tick_gameplay};
    const PersonSessionPolicy explicit_total_probe{false,true,3,50000,PersonWorkBudgetPolicy::total_probe};
    const PersonSessionPolicy displayed_total_probe{true,true,3,50000,PersonWorkBudgetPolicy::total_probe};
    require(integrated_headless.renews_work_budget()&&integrated_headless.tick_limit()==3&&
            integrated_headless.entity_budget(1000000)==PersonSessionPolicy::instructions&&
            integrated_headless.tick_budget(1000000,293)==2930000&&
            !integrated_headless.limit_reached(2,1000000)&&integrated_headless.limit_reached(3,1000000),
            "integrated headless gameplay renews work while retaining its explicit finite tick horizon");
    for(const auto used:{0ULL,9999ULL,10000ULL,1000000ULL}){
        require(integrated_headless.entity_budget(used)==bounded_game_window.entity_budget(used)&&
                integrated_headless.tick_budget(used,293)==bounded_game_window.tick_budget(used,293)&&
                integrated_headless.instruction_limit_reached(used)==bounded_game_window.instruction_limit_reached(used),
                "matching gameplay work does not depend on whether images are displayed");
    }
    require(!probe.renews_work_budget()&&window.renews_work_budget()&&
            !explicit_total_probe.renews_work_budget()&&!displayed_total_probe.renews_work_budget()&&
            explicit_total_probe.entity_budget(49999)==1&&displayed_total_probe.entity_budget(49999)==1&&
            explicit_total_probe.tick_budget(50000,293)==0&&displayed_total_probe.tick_budget(50000,293)==0&&
            explicit_total_probe.limit_reached(1,50000)&&displayed_total_probe.limit_reached(1,50000),
            "explicit diagnostic work policy overrides presentation and preserves its total cap");
    const PersonSessionPolicy continuous_total_probe{true,false,3,50000,PersonWorkBudgetPolicy::total_probe};
    require(continuous_total_probe.tick_limit()==std::numeric_limits<std::uint32_t>::max()&&
            continuous_total_probe.limit_reached(1,50000),
            "tick horizon and work cap are independent policy choices");
    std::cout<<"PASS gameplay work independent of presentation; explicit diagnostic override\n";

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

    // A finite integrated run must perform the same true-cycle work as the
    // displayed run, including when cumulative instructions exceed probe caps.
    odh::Vm headless_one({{root(),busy}}),headless_two({{root(),busy}});
    headless_one.start(root());headless_two.start(root());
    odh::EntityVmScheduler headless_scheduler(2);
    require(headless_scheduler.add(1,headless_one)&&headless_scheduler.add(2,headless_two),
            "integrated headless busy VMs registered");
    for(std::uint64_t tick=1;tick<=3;++tick){
        const auto used=headless_one.steps()+headless_two.steps();
        require(!integrated_headless.limit_reached(tick-1,used)&&
                headless_scheduler.tick(tick,integrated_headless.tick_budget(used,2),
                    integrated_headless.entity_budget(used)),
                "finite integrated gameplay renews actual VM work without a display");
        require(headless_one.steps()==tick*10000&&headless_two.steps()==tick*10000&&
                headless_scheduler.instructions_last_tick()==20000,
                "undisplayed true cycles receive the same bounded grants as displayed cycles");
    }
    require(headless_one.steps()==busy_one.steps()&&headless_two.steps()==busy_two.steps()&&
            headless_one.status()==busy_one.status()&&headless_two.status()==busy_two.status()&&
            integrated_headless.limit_reached(3,headless_one.steps()+headless_two.steps()),
            "finite undisplayed and displayed runs match VM state and retain the finite stopping condition");
    std::cout<<"PASS equal gameplay work for actual finite undisplayed VMs\n";

    // Real VMs expose the large-lot funding error: one ordinary action needs
    // 1000 instructions before waiting; every other object waits after one.
    // The old aggregate640k gave the first of744 runnable VMs only861, even
    // though the actual total work is1743. No busy loop or raised VM cap is
    // needed to reproduce that false budget interruption.
    const auto large_lot=[&](std::size_t count,bool legacy_cap,
                             const PersonSessionPolicy& policy){
        std::vector<std::uint64_t> calls(count);
        std::vector<std::unique_ptr<odh::Vm>> entities;entities.reserve(count);
        odh::EntityVmScheduler city(count);
        const auto action=routine({{33,0,255,{}}});
        const auto ordinary=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
                                const odh::Ts1Temps&,const odh::VmDispatch&){
            if(instruction.opcode!=33)return odh::PrimitiveOutcome{};
            const auto index=static_cast<std::size_t>(frame.context.caller-1);
            const auto limit=index==0?1000u:1u;
            return odh::PrimitiveOutcome{++calls.at(index)<limit?
                odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::wait,{}};
        };
        for(std::size_t n=0;n<count;++n){
            auto vm=std::make_unique<odh::Vm>(odh::RoutineSet{{root(),action}},
                odh::VmMemory{},odh::VmLimits{},ordinary);
            const auto id=static_cast<std::int16_t>(n+1);
            require(vm->start(root(),{id,id,id,0})==odh::VmStatus::running,
                    "large-lot ordinary VM starts");
            require(city.add(id,*vm),"large-lot VM registered");entities.push_back(std::move(vm));
        }
        const auto grant=legacy_cap?std::uint64_t{640000}:policy.tick_budget(0,city.size());
        require(city.tick(1,grant,policy.entity_budget(0),{},{},{},
                    odh::EntityVmScheduler::ControllerOrder::per_entity),
                "large-lot real scheduler tick executes");
        if(legacy_cap){
            const auto first=city.first_budget_yield();
            require(count==744&&city.budget_yield_count()==1&&first&&first->entity==1&&
                    first->granted==861&&first->used==861&&calls[0]==861&&
                    entities[0]->status()==odh::VmStatus::budget_exhausted&&
                    city.instructions_last_tick()==861+count-1,
                    "legacy64-slice cap spuriously truncates the first ordinary action with ample unused funding");
        }else{
            require(city.budget_yield_count()==0&&calls[0]==1000&&
                    city.instructions_last_tick()==1000+count-1,
                    "all ordinary large-lot actions finish their tick without a fabricated budget yield");
            for(std::size_t n=0;n<count;++n)
                require(entities[n]->status()==odh::VmStatus::waiting&&
                        calls[n]==(n==0?1000u:1u)&&
                        entities[n]->steps()<=PersonSessionPolicy::instructions,
                        "actual per-entity execution remains bounded by the unchanged10k safety slice");
        }
    };
    large_lot(744,true,integrated_headless);
    for(const auto count:{1u,64u,65u,293u,744u}){
        large_lot(count,false,integrated_headless);
        large_lot(count,false,bounded_game_window);
    }
    for(const auto count:{0u,64u,65u,744u})
        require(explicit_total_probe.tick_budget(0,count)==50000&&
                explicit_total_probe.tick_budget(49999,count)==1&&
                explicit_total_probe.tick_budget(50000,count)==0,
                "large entity counts do not expand the explicit diagnostic total budget");
    std::cout<<"PASS old744-VM starvation reproduced; funded1/64/65/293/744 gameplay matrix; total probes unchanged\n";

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
