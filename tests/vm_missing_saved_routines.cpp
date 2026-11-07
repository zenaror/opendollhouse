#include "odh/vm.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int checks=0;
void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
template<class F>void rejects(F call,const char* text){try{call();}catch(const std::invalid_argument&){return;}throw std::runtime_error(text);}
constexpr odh::CodeOwner owner=0x80000008U;
constexpr auto defer=odh::VmSavedRoutinePolicy::defer_missing_fault;
odh::Bhav routine(std::initializer_list<odh::Instruction> code){return {0x8002,code,{},odh::Bhav::Metadata{0,4,4,0}};}
std::vector<odh::VmFrame> frames(){
    return {{odh::routine_key(4096),7,{17,17,12,owner},{31},{41,42}},
            {odh::routine_key(4097),0,{17,17,13,owner},{0,0,0,0},{0,0,0,0}}};
}
}
int main(){try {
    using namespace odh;
    std::vector<std::uint16_t> dispatched;
    const VmPrimitive primitive=[&](const auto& frame,const auto& instruction,const auto&,const auto&){
        require(frame.key.id==4097,"missing saved routine never receives a manufactured instruction");
        dispatched.push_back(instruction.opcode);
        return PrimitiveOutcome{instruction.opcode==99?PrimitiveDisposition::wait:PrimitiveDisposition::goto_true,{}};};
    const auto code=std::make_shared<const VmProgram>(RoutineSet{{routine_key(4097),routine({{99,1,255,{}},{100,254,255,{}}})}});
    const auto make=[&](VmLimits limits=VmLimits{}){return std::make_unique<Vm>(std::make_shared<const VmProgram>(RoutineSet{}),
        ShareVmProgramTag{},VmMemory{},limits,primitive,SharedRoutineOwners{{owner,code}},ShareVmOwnerProgramsTag{});};
    Ts1Temps temps{};temps[3]=71;
    auto strict=make();
    rejects([&]{strict->restore_dormant_state(frames(),temps);},"strict dormant import accepted missing routine");
    require(strict->status()==VmStatus::ready && strict->stack().empty() && strict->run_generation()==0 && dispatched.empty(),
        "strict rejection leaves constructor VM untouched");
    auto imported=make();imported->restore_dormant_state(frames(),temps,defer);
    require(imported->status()==VmStatus::dormant && imported->steps()==0 && dispatched.empty() &&
        !imported->has_routine(routine_key(4096),owner) && imported->stack()[0].pc==7 &&
        imported->stack()[0].arguments==std::vector<std::int16_t>({31}) &&
        imported->stack()[0].locals==std::vector<std::int16_t>({41,42}) && imported->temps()==temps,
        "explicit admission preserves original missing frame without execution or invented metadata");
    const auto dormant_bytes=encode_vm_snapshot(imported->snapshot());
    const auto dormant=decode_vm_snapshot(dormant_bytes);
    auto strict_snapshot=make();require(!strict_snapshot->restore_snapshot(dormant),"snapshot restore remains strict by default");
    auto explicit_snapshot=make();require(explicit_snapshot->restore_snapshot(dormant,defer) &&
        encode_vm_snapshot(explicit_snapshot->snapshot())==dormant_bytes,
        "explicit missing-routine snapshot roundtrip needs no serialized policy flag");
    require(imported->activate_dormant_state() && imported->run(1)==VmStatus::waiting &&
        imported->pending() && dispatched==std::vector<std::uint16_t>({99}) && imported->stack().size()==2 &&
        imported->steps()==1,"valid top executes and waits while absent lower frame remains dormant");
    const auto waiting_bytes=encode_vm_snapshot(imported->snapshot());
    const auto waiting=decode_vm_snapshot(waiting_bytes);
    auto strict_wait=make();require(!strict_wait->restore_snapshot(waiting),"strict waiting snapshot still rejects absent lower frame");
    auto reopened=make();require(reopened->restore_snapshot(waiting,defer) &&
        encode_vm_snapshot(reopened->snapshot())==waiting_bytes && reopened->status()==VmStatus::waiting,
        "explicit policy restores a valid top wait above missing parent byte-for-byte");
    const auto ticket=reopened->pending()->ticket;
    require(reopened->complete(ticket,true) && reopened->status()==VmStatus::running &&
        reopened->stack().back().pc==1 && reopened->stack().size()==2 && dispatched.size()==1,
        "completion advances only valid top and does not anticipate parent failure");
    require(reopened->run(1)==VmStatus::fault && reopened->steps()==2 && reopened->stack().size()==1 &&
        reopened->stack()[0].key.id==4096 && reopened->stack()[0].pc==7 &&
        reopened->diagnostic()=="frame code-owner routine unavailable" && dispatched==std::vector<std::uint16_t>({99,100}),
        "missing parent faults exactly when returning valid child reaches it");
    auto missing_top=make();auto single=frames();single.resize(1);missing_top->restore_dormant_state(single,temps,defer);
    require(missing_top->activate_dormant_state() && missing_top->run(1)==VmStatus::fault &&
        missing_top->steps()==0 && dispatched.size()==2,"missing top faults at first fetch without dispatch or step invention");
    auto bad_wait=waiting;bad_wait.stack.back().key=routine_key(4098);bad_wait.pending->instruction.key=routine_key(4098);
    auto missing_wait=make();require(!missing_wait->restore_snapshot(bad_wait,defer) &&
        missing_wait->status()==VmStatus::ready && missing_wait->run_generation()==0,
        "wait on its own missing routine rejects cleanly without throwing from validation");
    const auto reject_dormant=[&](auto mutate,const char* text){auto bad=frames();mutate(bad);auto target=make();
        rejects([&]{target->restore_dormant_state(bad,temps,defer);},text);
        require(target->status()==VmStatus::ready && target->run_generation()==0 && target->stack().empty(),
            "invalid deferred dormant import leaves destination untouched");};
    reject_dormant([](auto& f){f.back().pc=2;},"deferred policy accepted out-of-range existing PC");
    reject_dormant([](auto& f){f.front().context.code_owner=999;},"deferred policy accepted missing code owner");
    reject_dormant([](auto& f){f.front().arguments.resize(4097);},"deferred policy accepted oversized missing arguments");
    reject_dormant([](auto& f){f.front().locals.resize(4097);},"deferred policy accepted oversized missing locals");
    reject_dormant([](auto& f){f.front().return_mode=VmCallReturnMode::interaction_next_tick;},"deferred missing frame accepted nonordinary return");
    reject_dormant([](auto& f){f.front().invocation_ticket=1;},"deferred missing frame accepted invocation ticket");
    reject_dormant([](auto& f){f.front().key=routine_key(255);},"deferred policy accepted absent primitive-range routine ID");
    reject_dormant([](auto& f){f.front().key.scope=RoutineScope::semiglobal;},"deferred policy accepted inconsistent routine scope");
    const auto reject_snapshot=[&](auto mutate,const char* text){auto bad=waiting;mutate(bad);auto target=make();
        require(!target->restore_snapshot(bad,defer),text);
        require(target->status()==VmStatus::ready && target->run_generation()==0 && target->stack().empty(),
            "invalid deferred snapshot leaves destination untouched");};
    reject_snapshot([](auto& s){s.stack.back().pc=2;},"deferred snapshot accepted invalid existing PC");
    reject_snapshot([](auto& s){s.stack.front().context.code_owner=999;},"deferred snapshot accepted unknown owner");
    reject_snapshot([](auto& s){s.stack.front().arguments.resize(4097);},"deferred snapshot accepted overbound missing variables");
    reject_snapshot([](auto& s){s.stack.front().return_mode=VmCallReturnMode::interaction_next_tick;s.stack.front().invocation_ticket=1;},
        "deferred snapshot accepted nonordinary missing frame");
    reject_snapshot([](auto& s){s.stack.front().key=routine_key(255);},"deferred snapshot accepted absent routine <=255");
    for(const auto bad_code:{Bhav{0x8002,{{100,254,255,{}}},{},{}},routine({}),routine({{100,2,255,{}}})}) {
        Vm bad({}, {}, {}, {},RoutineOwners{{owner,{{routine_key(4097),bad_code}}}});
        rejects([&]{bad.restore_dormant_state(frames(),temps,defer);},"deferred policy accepted existing bad metadata/empty routine/branch");
        require(!bad.restore_snapshot(waiting,defer),"deferred snapshot accepted existing bad metadata/empty routine/branch");
    }
    {
        constexpr auto defer_pc=VmSavedRoutinePolicy::defer_missing_or_pc_fault;
        const auto pc_code=std::make_shared<const VmProgram>(RoutineSet{
            {routine_key(4096),routine({{101,254,255,{}}})},
            {routine_key(4097),routine({{99,1,255,{}},{100,254,255,{}}})}});
        const auto make_pc=[&]{return std::make_unique<Vm>(std::make_shared<const VmProgram>(RoutineSet{}),
            ShareVmProgramTag{},VmMemory{},VmLimits{},primitive,SharedRoutineOwners{{owner,pc_code}},ShareVmOwnerProgramsTag{});};
        auto pc_frames=frames();pc_frames.front().arguments.resize(4,0);pc_frames.front().locals.resize(4,0);
        auto single_pc=pc_frames;single_pc.resize(1);
        for(const auto policy:{VmSavedRoutinePolicy::require_present,defer}) {
            auto strict_pc=make_pc();rejects([&]{strict_pc->restore_dormant_state(single_pc,temps,policy);},
                "default and missing-only policies retain strict existing-PC checks");
        }
        auto invalid_top=make_pc();invalid_top->restore_dormant_state(single_pc,temps,defer_pc);
        const auto pc_dormant_bytes=encode_vm_snapshot(invalid_top->snapshot());
        require(invalid_top->status()==VmStatus::dormant && invalid_top->steps()==0 &&
            invalid_top->stack().front().pc==7 && invalid_top->stack().front().arguments==single_pc.front().arguments &&
            invalid_top->stack().front().locals==single_pc.front().locals,"new policy preserves invalid PC without dispatch or fabricated code");
        const auto pc_dormant=decode_vm_snapshot(pc_dormant_bytes);
        for(const auto policy:{VmSavedRoutinePolicy::require_present,defer}) {
            auto strict_pc=make_pc();require(!strict_pc->restore_snapshot(pc_dormant,policy),
                "old snapshot policies reject preserved invalid existing PC");
        }
        auto pc_reopen=make_pc();require(pc_reopen->restore_snapshot(pc_dormant,defer_pc) &&
            encode_vm_snapshot(pc_reopen->snapshot())==pc_dormant_bytes,"invalid-PC snapshot remains wire byte-stable with explicit host policy");
        const auto before_calls=dispatched.size();
        require(invalid_top->activate_dormant_state() && invalid_top->run(1)==VmStatus::fault &&
            invalid_top->steps()==0 && invalid_top->diagnostic()=="instruction pointer outside routine" &&
            dispatched.size()==before_calls,"invalid-PC top faults exactly at first fetch without consuming a fabricated instruction");
        auto pc_lower=make_pc();pc_lower->restore_dormant_state(pc_frames,temps,defer_pc);
        require(pc_lower->activate_dormant_state() && pc_lower->run(1)==VmStatus::waiting &&
            pc_lower->pending() && pc_lower->steps()==1 && pc_lower->stack().size()==2,
            "valid top waits before lower invalid PC is reached");
        const auto pc_wait_bytes=encode_vm_snapshot(pc_lower->snapshot());const auto pc_wait=decode_vm_snapshot(pc_wait_bytes);
        auto pc_wait_reopen=make_pc();require(pc_wait_reopen->restore_snapshot(pc_wait,defer_pc) &&
            encode_vm_snapshot(pc_wait_reopen->snapshot())==pc_wait_bytes,"valid top wait reopens above lower invalid PC");
        for(const auto policy:{VmSavedRoutinePolicy::require_present,defer}) {
            auto old=make_pc();require(!old->restore_snapshot(pc_wait,policy),"old policies reject lower invalid PC even beneath valid pending frame");
        }
        require(pc_wait_reopen->complete(pc_wait_reopen->pending()->ticket,true) &&
            pc_wait_reopen->status()==VmStatus::running && pc_wait_reopen->stack().back().pc==1,
            "valid wait completion does not anticipate invalid lower PC");
        require(pc_wait_reopen->run(1)==VmStatus::fault && pc_wait_reopen->steps()==2 &&
            pc_wait_reopen->stack().size()==1 && pc_wait_reopen->stack().front().pc==7 &&
            pc_wait_reopen->diagnostic()=="instruction pointer outside routine" && dispatched.size()==before_calls+2,
            "returning valid top faults at invalid parent access rather than throwing std::out_of_range");
        auto own_bad_pc=pc_wait;own_bad_pc.stack.back().pc=2;own_bad_pc.pending->instruction.pc=2;
        auto own_bad=make_pc();require(!own_bad->restore_snapshot(own_bad_pc,defer_pc),
            "new policy never accepts pending wait on its own invalid PC");
        auto still_missing=make();require(still_missing->restore_snapshot(dormant,defer_pc),
            "new PC policy includes explicit missing-routine admission");
        for(const auto bad_code:{Bhav{0x8002,{{100,254,255,{}}},{},{}},routine({}),routine({{100,2,255,{}}})}) {
            Vm bad({}, {}, {}, {},RoutineOwners{{owner,{{routine_key(4097),bad_code}}}});
            rejects([&]{bad.restore_dormant_state(frames(),temps,defer_pc);},"PC policy cannot bypass metadata/empty/branch validation");
            require(!bad.restore_snapshot(waiting,defer_pc),"PC snapshot policy keeps metadata/empty/branch guards");
        }
        for(const unsigned invalid:{0U,1U,2U}) {
            auto bad=pc_frames;
            if(invalid==0)bad.front().context.code_owner=999;
            if(invalid==1)bad.front().arguments.resize(4097);
            if(invalid==2)bad.front().key.scope=RoutineScope::semiglobal;
            auto target=make_pc();rejects([&]{target->restore_dormant_state(bad,temps,defer_pc);},
                "PC policy retains owner, variable-budget and scope guards");
            auto snapshot=pc_wait;snapshot.stack=bad;
            require(!target->restore_snapshot(snapshot,defer_pc),"PC snapshot policy retains owner, variable-budget and scope guards");
        }
    }
    auto invalid_policy=make();
    require(!invalid_policy->restore_snapshot(dormant,static_cast<VmSavedRoutinePolicy>(99)),"unknown snapshot admission policy rejected");
    rejects([&]{invalid_policy->restore_dormant_state(frames(),temps,static_cast<VmSavedRoutinePolicy>(99));},"unknown dormant admission policy rejected");
    std::cout<<"PASS missing saved routine policy ("<<checks<<" checks)\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
