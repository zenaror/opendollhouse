#include "odh/vm.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/formats.hpp"
#include <iostream>
#include <string>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
template<class F> bool rejects(F f){try{f();}catch(const odh::FormatError&){return true;}return false;}
const auto key=odh::routine_key(4096),child_key=odh::routine_key(4097);
odh::Bhav routine(std::initializer_list<odh::Instruction> code) {
    return {0x8002,code,{},odh::Bhav::Metadata{0,4,2,0}};
}
odh::RoutineSet program(std::uint8_t branch=1) {
    return {{key,routine({{18,branch,255,{}},{2,254,255,{1,0,77,0,0,5,8,7}}})}};
}
odh::PrimitiveOutcome yield() {
    odh::PrimitiveOutcome out{odh::PrimitiveDisposition::goto_true_next_tick,{}};
    out.write=odh::VmFrameWrite{odh::VmFrameBank::arguments,0,123};
    out.temp_write=odh::VmTempWrite{0,456};
    out.stack_object_write=odh::VmStackObjectWrite{3};
    return out;
}
}
int main(){try{
    static_assert(static_cast<int>(odh::VmStatus::yielded)==8);
    static_assert(static_cast<int>(odh::VmStatus::next_tick)==9);
    static_assert(static_cast<int>(odh::PrimitiveDisposition::call)==4);
    static_assert(static_cast<int>(odh::PrimitiveDisposition::goto_true_next_tick)==5);
    {
        unsigned calls=0;
        const odh::VmPrimitive retry=[&](const auto&,const auto&,const auto&,const auto&){
            ++calls;return odh::PrimitiveOutcome{calls==1?odh::PrimitiveDisposition::continue_next_tick:
                odh::PrimitiveDisposition::goto_true,{}};
        };
        odh::Vm source(program(),{}, {},retry);source.start(key,{2,2,2});
        require(source.run(20)==odh::VmStatus::next_tick&&source.stack().back().pc==0&&calls==1&&!source.pending(),
            "CONTINUE_NEXT_TICK retains exact PC and creates no asynchronous wait");
        const auto wire=odh::encode_vm_snapshot(source.snapshot());
        odh::Vm restored(program(),{}, {},retry);
        require(restored.restore_snapshot(odh::decode_vm_snapshot(wire))&&restored.stack().back().pc==0&&
            odh::encode_vm_snapshot(restored.snapshot())==wire,"same-PC next-tick state restores with existing wire profile");
        require(restored.resume_next_tick()&&restored.run(20)==odh::VmStatus::returned_true&&calls==2,
            "next host tick reexecutes retained primitive before selecting its real branch");
        odh::Vm checked(program());checked.start(key,{2,2,2});
        const auto result=checked.run_synchronous_check(20,[](const auto&,const auto&,const auto&,const auto&){
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::continue_next_tick,{}};
        });
        require(result.started&&result.status==odh::VmStatus::fault&&result.diagnostic.find("next tick")!=std::string::npos,
            "synchronous check refuses same-PC next-tick continuation");
    }
    unsigned dispatched=0;bool guarded=false;odh::Vm* active=nullptr;
    const odh::VmPrimitive handler=[&](const auto&,const auto&,const auto&,const auto&){
        ++dispatched;
        if(active)try{active->resume_next_tick();}catch(const std::logic_error&){guarded=true;}
        return yield();
    };
    odh::Vm vm(program(),{}, {},handler);active=&vm;
    require(!vm.resume_next_tick(),"fresh VM cannot fabricate next-tick resume");
    vm.start(key,{2,2,2},{9});vm.temps()[2]=789;vm.temp_xl()[0]=123456;
    const auto running_wire=odh::encode_vm_snapshot(vm.snapshot());
    require(running_wire[0]==6&&running_wire[1]==0,"new runtime snapshots persist known ActionTree state in version6");
    auto legacy_running=vm.snapshot();legacy_running.action_tree_known=false;
    const auto legacy_running_wire=odh::encode_vm_snapshot(legacy_running);
    require(legacy_running_wire[0]==2&&odh::encode_vm_snapshot(odh::decode_vm_snapshot(legacy_running_wire))==legacy_running_wire,
        "untagged legacy ordinary snapshot retains byte-exact version2");
    require(vm.run(20)==odh::VmStatus::next_tick&&guarded&&dispatched==1&&vm.steps()==1&&
        vm.stack().size()==1&&vm.stack()[0].pc==1&&!vm.pending()&&vm.temps()[1]==0,
        "TRUE branch stops slice before following instruction despite ample budget");
    require(vm.stack()[0].arguments[0]==123&&vm.temps()[0]==456&&vm.stack()[0].context.stack_object==3,
        "next-tick branch publishes validated frame temp and context outputs");
    const auto saved=vm.snapshot();const auto bytes=odh::encode_vm_snapshot(saved);
    require(bytes[0]==6&&bytes[1]==0&&bytes[2]==1&&bytes[3]==9&&std::string(odh::vm_status_name(vm.status()))=="next_tick",
        "next-tick status persists alongside known ActionTree metadata");
    auto legacy_saved=saved;legacy_saved.action_tree_known=false;
    const auto legacy_bytes=odh::encode_vm_snapshot(legacy_saved);
    require(legacy_bytes[0]==3&&legacy_bytes[2]==9&&
        odh::encode_vm_snapshot(odh::decode_vm_snapshot(legacy_bytes))==legacy_bytes,
        "legacy next-tick state retains byte-exact version3");
    require(vm.run(20)==odh::VmStatus::next_tick&&vm.steps()==1&&dispatched==1,
        "run cannot implicitly release next-tick boundary");
    active=nullptr;
    odh::Vm copy(program(),{}, {},handler);
    require(copy.restore_snapshot(odh::decode_vm_snapshot(bytes))&&
        odh::encode_vm_snapshot(copy.snapshot())==bytes&&copy.stack()[0].arguments[0]==123&&
        copy.stack()[0].locals==saved.stack[0].locals&&copy.temps()[2]==789&&copy.temp_xl()[0]==123456,
        "strict restore preserves dormant next-tick frame variables registers and tickets byte-for-byte");
    require(vm.resume_next_tick()&&copy.resume_next_tick()&&!vm.resume_next_tick()&&
        vm.run(20)==odh::VmStatus::returned_true&&copy.run(20)==odh::VmStatus::returned_true&&
        vm.temps()[1]==77&&vm.steps()==2&&dispatched==1,
        "explicit host release executes continuation once without redispatching primitive");
    require(odh::encode_vm_snapshot(vm.snapshot())==odh::encode_vm_snapshot(copy.snapshot())&&
        odh::encode_vm_snapshot(vm.snapshot())[0]==6,"completed runtime continuation retains known ActionTree metadata");
    odh::Vm terminal(program(254),{}, {},handler);terminal.start(key,{2,2,2});
    require(terminal.run(20)==odh::VmStatus::returned_true&&terminal.stack().empty()&&!terminal.resume_next_tick(),
        "terminal TRUE return is not overwritten by next-tick state");
    odh::Vm terminal_false(program(255),{}, {},handler);terminal_false.start(key,{2,2,2});
    require(terminal_false.run(20)==odh::VmStatus::returned_false&&!terminal_false.resume_next_tick(),
        "TRUE pointer to RETURN_FALSE also preserves terminal result");
    odh::Vm interaction({{key,routine({{19,254,255,{}}})},{child_key,routine({{18,254,255,{}}})}}, {}, {},
        [](const auto& f,const auto& i,const auto&,const auto&)->odh::PrimitiveOutcome{
            if(i.opcode==18)return yield();
            return {odh::PrimitiveDisposition::call,{}, {},odh::VmCall{child_key,f.context,{},
                odh::VmCallReturnMode::interaction_next_tick}};
        });
    interaction.start(key,{2,2,2});
    require(interaction.run(20)==odh::VmStatus::yielded&&interaction.interaction_return()&&
        !interaction.resume_next_tick(),"interaction return handoff is preserved when TRUE branch pops its child");
    odh::Vm check(program());check.start(key,{2,2,2},{9});
    const auto result=check.run_synchronous_check(20,handler);
    require(result.started&&result.status==odh::VmStatus::fault&&
        result.diagnostic.find("next tick")!=std::string::npos&&check.stack()[0].arguments[0]==9&&check.temps()[0]==0,
        "synchronous checks explicitly reject next-tick disposition before register outputs");
    auto bad_bytes=legacy_bytes;bad_bytes[0]=2;
    require(rejects([&]{odh::decode_vm_snapshot(bad_bytes);}),"old wire version rejects new status");
    bad_bytes=bytes;bad_bytes[0]=9;
    require(rejects([&]{odh::decode_vm_snapshot(bad_bytes);}),"unknown future wire version rejects");
    bad_bytes=bytes;bad_bytes[3]=10;
    require(rejects([&]{odh::decode_vm_snapshot(bad_bytes);}),"unknown status rejects");
    auto bad=saved;bad.pending=odh::VmPending{};
    require(rejects([&]{odh::encode_vm_snapshot(bad);}),"next-tick status cannot carry pending service wait");
    bad=saved;bad.stack.clear();
    require(rejects([&]{odh::encode_vm_snapshot(bad);}),"next-tick status requires continuation stack");
    bad=saved;bad.stack[0].pc=999;
    odh::Vm invalid(program());
    require(!invalid.restore_snapshot(bad),"strict restore does not relax existing routine PC validation");
    for(std::size_t n=0;n<bytes.size();++n) {
        auto cut=bytes;cut.resize(n);
        require(rejects([&]{odh::decode_vm_snapshot(cut);}),"truncated next-tick wire rejected");
    }
    bad_bytes=bytes;bad_bytes.push_back(0);
    require(rejects([&]{odh::decode_vm_snapshot(bad_bytes);}),"trailing next-tick wire rejected");
    std::cout<<checks<<" next-tick VM checks passed (synthetic fixtures)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
