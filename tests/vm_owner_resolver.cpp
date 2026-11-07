#include "odh/vm.hpp"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
odh::RoutineKey key(unsigned id){return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Instruction assign(unsigned temp,unsigned value){return {2,254,255,{
    static_cast<std::uint8_t>(temp),0,static_cast<std::uint8_t>(value),0,0,5,8,7}};}
odh::Bhav routine(std::initializer_list<odh::Instruction> code){return {0x8002,code,{},odh::Bhav::Metadata{0,4,0,0}};}
odh::RoutineSet base(){return {{key(256),routine({{19,254,255,{}}})},
    {key(4096),routine({assign(0,7)})},{key(8192),routine({assign(1,9)})}};}
std::shared_ptr<const odh::VmProgram> owner_bank(unsigned value=44){
    return std::make_shared<const odh::VmProgram>(odh::RoutineSet{
        {key(4096),routine({{8192,1,255,{}},assign(0,value)})},
        {key(8192),routine({assign(1,value+1)})}});
}
odh::VmPrimitive call_owner(unsigned owner){return [owner](const auto& f,const auto&,const auto&,const auto&){
    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
        odh::VmCall{key(4096),{f.context.caller,f.context.callee,f.context.stack_object,owner},{}}};
};}
template<class F> bool throws_logic(F f){try{f();}catch(const std::logic_error&){return true;}return false;}
void primitive_owner_admission(){
    odh::Vm* live=nullptr;unsigned lookups=0;bool guarded=false;
    std::optional<odh::VmFrame> captured_frame;std::optional<odh::Instruction> captured_instruction;
    std::optional<odh::VmDispatch> captured_dispatch;
    odh::VmLimits limits;limits.code_owners=2;limits.stack_depth=1;
    const odh::VmPrimitive primitive=[&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
        captured_frame=frame;captured_instruction=instruction;captured_dispatch=dispatch;
        auto changed=instruction;++changed.operands[0];
        auto wrong=dispatch;++wrong.ticket;
        auto check=dispatch;check.check_mode=true;
        auto foreign=dispatch;foreign.origin=nullptr;
        auto other_frame=frame;++other_frame.context.callee;
        require(!live->prepare_primitive_owner(frame,changed,dispatch,44)&&
            !live->prepare_primitive_owner(frame,instruction,wrong,44)&&
            !live->prepare_primitive_owner(frame,instruction,check,44)&&
            !live->prepare_primitive_owner(frame,instruction,foreign,44)&&
            !live->prepare_primitive_owner(other_frame,instruction,dispatch,44)&&lookups==0,
            "owner-only admission refuses forged instruction ticket check origin and frame before resource lookup");
        bool checked=false;
        const auto check_result=live->evaluate_check(key(256),frame.context,{},8,
            [&](const auto& check_frame,const auto& operation,const auto&,const auto& proof){
                checked=proof.check_mode&&!const_cast<odh::Vm*>(proof.origin)->prepare_primitive_owner(
                    check_frame,operation,proof,44);
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });
        require(check_result.started&&check_result.status==odh::VmStatus::returned_true&&checked&&lookups==0,
            "real synchronous Check dispatch cannot widen owner visibility or invoke resolver");
        require(!live->can_accept_call({key(4096),{1,1,1,44},{}}),
            "fixture cannot push a child at its depth budget");
        require(live->prepare_primitive_owner(frame,instruction,dispatch,44)&&
            live->has_routine(key(4096),44)&&live->stack().size()==1&&lookups==1,
            "real primitive can admit an exact owner without pushing or requiring child stack capacity");
        require(live->prepare_primitive_owner(frame,instruction,dispatch,44)&&lookups==1,
            "already admitted owner remains idempotent without replaying resolver");
        bool capacity=false;
        try{live->prepare_primitive_owner(frame,instruction,dispatch,45);}catch(const std::exception&){capacity=true;}
        require(capacity&&lookups==1&&!live->has_routine(key(4096),45),
            "owner-only admission preserves capacity before any second resource lookup");
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
    };
    odh::Vm vm(base(),{},limits,primitive);live=&vm;
    require(vm.set_owner_resolver([&](auto owner){
        ++lookups;require(owner==44,"owner-only resolver receives the requested resource identity");
        guarded=!vm.prepare_primitive_owner(*captured_frame,*captured_instruction,*captured_dispatch,45)&&
            !vm.prepare_primitive_call(*captured_frame,*captured_instruction,*captured_dispatch,
                {key(4096),{1,1,1,45},{}})&&!vm.install_shared_owner(45,owner_bank(45))&&
            !vm.set_owner_resolver({})&&throws_logic([&]{vm.run(1);});
        return owner_bank();
    }),"bind authenticated primitive-only owner resolver");
    vm.start(key(256),{1,1,1});
    require(vm.run(8)==odh::VmStatus::returned_true&&guarded&&vm.temps()[0]==0&&vm.temps()[1]==0,
        "owner admission preserves Main result registers and blocks resolver reentry");
    require(!vm.prepare_primitive_owner(*captured_frame,*captured_instruction,*captured_dispatch,44)&&lookups==1,
        "formerly valid primitive proof cannot admit or authenticate an owner after dispatch ends");
}
void long_lived_owner_cache() {
    const auto resident=owner_bank(42);
    odh::Vm vm(base());
    std::map<odh::CodeOwner,std::shared_ptr<const odh::VmProgram>> registry{{42,resident}};
    const auto resolve=[&](auto id){return registry.at(id);};
    require(vm.set_owner_resolver(resolve)&&vm.start(key(4096),{1,1,1,42})==odh::VmStatus::running,
        "resident starts its primary frame");
    for(unsigned owner=100;owner<180;++owner){
        registry.emplace(owner,owner_bank(owner));
        require(vm.install_shared_owner(owner,registry.at(owner)),"churn admits each exact owner");
        require(!vm.release_idle_owner(42,resident)&&!vm.release_idle_owner(owner,resident),
            "active primary and mismatched bank remain installed");
        require(vm.release_idle_owner(owner,registry.at(owner))&&!vm.has_routine(key(4096),owner),
            "idle exact bank releases one slot without touching an active frame");
    }
    const auto snapshot=vm.snapshot();
    odh::Vm restored(base());
    require(restored.set_owner_resolver(resolve)&&restored.install_shared_owner(42,resident)&&
        restored.restore_snapshot(snapshot)&&restored.run(16)==odh::VmStatus::returned_true&&
        restored.temps()[0]==42,"continuation runs through a fresh restore after more than 64 distinct banks");
    require(restored.start(key(4096),{1,1,1,179})==odh::VmStatus::running&&
        restored.run(16)==odh::VmStatus::returned_true&&restored.temps()[0]==179,
        "historical ID lazily resolves to its original contents again");

    // The child has left the stack, but its return ticket still names its bank.
    odh::Vm interaction(base(),{},{},[](const auto&,const auto&,const auto&,const auto&){
        return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
            odh::VmCall{key(4096),{1,1,1,42},{},odh::VmCallReturnMode::interaction_next_tick}};
    });
    require(interaction.set_owner_resolver(resolve)&&interaction.start(key(256),{1,1,1})==
        odh::VmStatus::running&&interaction.run(16)==odh::VmStatus::yielded&&interaction.interaction_return(),
        "child returns to the interaction controller");
    const auto ticket=interaction.interaction_return()->ticket;
    require(!interaction.release_idle_owner(42,resident)&&interaction.resume_interaction(ticket)&&
        interaction.release_idle_owner(42,resident),"unconsumed return keeps its bank after the frame is popped");

}

}
int main(){try{
    primitive_owner_admission();
    long_lived_owner_cache();
    std::map<odh::CodeOwner,std::shared_ptr<const odh::VmProgram>> registry;
    for(unsigned owner=1;owner<=80;++owner)registry.emplace(owner,owner_bank(owner));
    const auto baseline_count=registry.at(44).use_count();std::vector<odh::CodeOwner> requests;
    odh::VmLimits limits;limits.code_owners=2; // default plus only the demanded owner.
    {
        odh::Vm vm(base(),{},limits,call_owner(44));
        require(vm.set_owner_resolver([&](auto owner){requests.push_back(owner);return registry.at(owner);}),"bind exact registry resolver");
        require(!vm.has_routine(key(4096),44)&&!vm.can_accept_call({key(4096),{1,1,1,44},{}})&&requests.empty(),
            "nonmutating routine/admission queries do not load resource owners");
        vm.start(key(256),{1,1,1});
        require(vm.run(20)==odh::VmStatus::returned_true&&vm.temps()[0]==44&&vm.temps()[1]==45&&
            requests==std::vector<odh::CodeOwner>{44},"primitive child demand loads only exact owner and executes its private plus semiglobal bank");
        require(registry.at(44).use_count()==baseline_count+1&&registry.at(43).use_count()==1,
            "VM shares immutable requested bank without copying or loading other registry entries");
        vm.start(key(4096),{1,1,1,44});
        require(vm.run(20)==odh::VmStatus::returned_true&&requests.size()==1,"repeated owner calls use cached bank");
        require(vm.start(key(4096),{1,1,1,45})==odh::VmStatus::fault&&
            vm.diagnostic().find("capacity")!=std::string::npos&&requests.size()==1&&!vm.has_routine(key(4096),45),
            "owner cap rejects before invoking resolver or widening registry admission");
    }
    require(registry.at(44).use_count()==baseline_count,"VM destruction releases its shared cache ownership");
    odh::Vm unchanged(base());
    require(unchanged.start(key(4096),{1,1,1,44})==odh::VmStatus::fault&&
        unchanged.diagnostic().find("code owner not supplied")!=std::string::npos,"default missing-owner behavior remains explicit");
    require(unchanged.start(key(4096),{1,1,1})==odh::VmStatus::running&&unchanged.run(10)==odh::VmStatus::returned_true&&
        unchanged.temps()[0]==7,"default owner0 execution remains unchanged");
    for(unsigned kind=0;kind<4;++kind){
        odh::Vm vm(base());unsigned calls=0;
        vm.set_owner_resolver([&](auto)->std::shared_ptr<const odh::VmProgram>{
            ++calls;
            if(kind==0)return {};
            if(kind==1)return std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
            if(kind==2)return std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(256),routine({assign(0,1)})}});
            return std::make_shared<const odh::VmProgram>(odh::RoutineSet{
                {{odh::RoutineScope::private_object,8192},routine({assign(0,1)})}});
        });
        require(vm.start(key(4096),{1,1,1,44})==odh::VmStatus::fault&&calls==1&&
            vm.diagnostic().find("invalid bank")!=std::string::npos&&!vm.has_routine(key(4096),44),
            "null empty global-containing and mismatched-scope banks refuse without owner0 fallback");
    }
    odh::Vm missing_routine(base());missing_routine.set_owner_resolver([](auto){
        return std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(8192),routine({assign(0,1)})}});
    });
    require(missing_routine.start(key(4096),{1,1,1,44})==odh::VmStatus::fault&&
        missing_routine.diagnostic().find("routine not supplied")!=std::string::npos,
        "valid exact bank missing requested private routine cannot borrow default owner's routine");
    for(bool standard:{false,true}){
        odh::Vm vm(base(),{}, {},call_owner(44));
        vm.set_owner_resolver([standard](auto)->std::shared_ptr<const odh::VmProgram>{
            if(standard)throw std::runtime_error("resource lookup failed");throw 7;
        });
        require(vm.start(key(4096),{1,1,1,44})==odh::VmStatus::fault&&vm.diagnostic().find("resolver failed")!=std::string::npos,
            "resolver exceptions during start become explicit VM faults");
        vm.start(key(256),{1,1,1});
        require(vm.run(20)==odh::VmStatus::fault&&vm.diagnostic().find("resolver failed")!=std::string::npos,
            "resolver exceptions during child push become explicit VM faults");
    }
    for(bool during_run:{false,true}){
        odh::Vm vm(base(),{}, {},call_owner(44));bool protected_calls=false;
        vm.set_owner_resolver([&](auto){
            protected_calls=vm.dispatch_active()&&!vm.set_owner_resolver({})&&!vm.install_shared_owner(45,registry.at(45))&&
                !vm.reset_execution_state()&&throws_logic([&]{vm.start(key(4096));})&&
                throws_logic([&]{vm.run(1);})&&throws_logic([&]{vm.snapshot();});
            return registry.at(44);
        });
        const auto started=vm.start(during_run?key(256):key(4096),{1,1,1,during_run?0U:44U});
        require(started==odh::VmStatus::running&&vm.run(20)==odh::VmStatus::returned_true&&protected_calls&&vm.temps()[0]==44,
            "resolver reentry guards protect both ordinary dispatch and start outside run");
    }
    odh::Vm source(base());source.install_shared_owner(44,registry.at(44));source.start(key(4096),{1,1,1,44});
    const auto saved=source.snapshot();
    odh::Vm restored(base());unsigned restore_lookups=0;
    restored.set_owner_resolver([&](auto){++restore_lookups;return registry.at(44);});
    require(!restored.restore_snapshot(saved)&&restore_lookups==0,"strict snapshot restore still requires preloaded declared owner manifests");
    bool dormant_rejected=false;
    try{restored.restore_dormant_state(saved.stack,saved.temps);}catch(const std::exception&){dormant_rejected=true;}
    require(dormant_rejected&&restore_lookups==0,"dormant restore also cannot implicitly consult resolver");
    require(restored.install_shared_owner(44,registry.at(44))&&restored.restore_snapshot(saved)&&
        restored.run(20)==odh::VmStatus::returned_true&&restore_lookups==0,
        "manifest-backed explicit owner installation permits strict restore without resolver use");
    require(restored.set_owner_resolver({})&&restored.start(key(4096),{1,1,1,45})==odh::VmStatus::fault,
        "host can clear callback at safe boundary without clearing admitted banks");
    std::cout<<checks<<" VM owner resolver checks passed (native resource-bank fixtures)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
