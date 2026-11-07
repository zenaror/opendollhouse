#include "odh/object_removal.hpp"
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
const auto key=odh::routine_key(4096);
odh::Instruction instruction(std::uint16_t selector=0,std::uint8_t flags=0) {
    return {18,1,255,{static_cast<std::uint8_t>(selector),static_cast<std::uint8_t>(selector>>8),flags,0,0,0,0,0}};
}
odh::RoutineSet program(const odh::Instruction& i) {
    return {{key,odh::Bhav{0x8002,{i,{2,254,255,{0,0,77,0,0,5,8,7}}},{},
        odh::Bhav::Metadata{0,4,0,0}}}};
}
void bound_reference_removal(){
    odh::World world;
    require(world.create(1,odh::EntityKind::object,{})&&world.create(2,odh::EntityKind::object,{})&&
        world.create(3,odh::EntityKind::object,{}),"bound deletion identities");
    const odh::WorldEntityReference caller{1,world.find(1)->generation},old{2,world.find(2)->generation};
    require(world.remove_retaining_body(old)&&world.create(2,odh::EntityKind::object,{}),"old dead body and same numeric live replacement");
    const odh::WorldEntityReference current{2,world.find(2)->generation};
    std::vector<odh::ObjectRemovalRequest> admitted;
    auto handler=odh::object_removal_primitive(world,[&](const auto& request,const auto&)->std::optional<std::string>{
        admitted.push_back(request);return {};});
    const auto context=[&](odh::WorldEntityReference actor,odh::WorldEntityReference target){
        odh::VmContext result{actor.id,1,target.id,0,true,actor.generation,caller.generation,target.generation};return result;
    };
    const auto run=[&](odh::VmContext source,std::uint16_t selector,odh::VmStatus expected,bool schedules,bool self){
        admitted.clear();odh::Vm vm(program(instruction(selector,255)),world.vm_memory(),{},handler);
        vm.start(key,source);
        require(vm.run(20)==expected,"resolved pointer identity chooses the source continuation branch");
        require(admitted.size()==(schedules?1U:0U),"only exact indexed non-Dead targets request actual deletion");
        if(schedules)require(admitted[0].target==2&&admitted[0].generation==current.generation&&
            admitted[0].caller==self&&admitted[0].flags==255,"admission retains exact live generation and pointer-based caller identity");
        require(world.find(2)&&world.find(2)->generation==current.generation,"old references never remove or rebind the replacement");
    };
    run(context(caller,old),1,odh::VmStatus::returned_true,false,false);
    run(context(old,old),1,odh::VmStatus::next_tick,false,true);
    run(context(old,current),1,odh::VmStatus::returned_true,true,false);
    run(context(current,old),1,odh::VmStatus::returned_true,false,false);
    run(context(current,current),1,odh::VmStatus::next_tick,true,true);
    run(context(old,current),0,odh::VmStatus::next_tick,false,true);
    run(context(caller,{2,0}),1,odh::VmStatus::returned_true,false,false);
    run(context({2,0},current),1,odh::VmStatus::returned_true,true,false);
    run(context({7,0},{8,0}),1,odh::VmStatus::next_tick,false,true);
    run(context(old,{2,0}),1,odh::VmStatus::returned_true,false,false);
    require(world.mark_dead({3,world.find(3)->generation}),"source indexed Dead window");
    run(context(caller,{3,world.find(3)->generation}),1,odh::VmStatus::returned_true,false,false);
    require(world.collect_retained_bodies({}),"collect no-longer-rooted old body for unavailable-reference negative cases");
    run(context(caller,old),1,odh::VmStatus::fault,false,false);
    run(context(old,current),1,odh::VmStatus::fault,false,false);
    run(context(old,current),0,odh::VmStatus::fault,false,false);
    admitted.clear();
    auto unauthenticated=odh::object_removal_primitive(world,[&](const auto& request,const auto&)->std::optional<std::string>{admitted.push_back(request);return {};});
    odh::Vm proof(program(instruction()),world.vm_memory(),{},
        [&](const auto& frame,const auto& authored,const auto& temps,const auto& dispatch){
            auto fake=frame;++fake.context.caller_generation;
            require(unauthenticated(fake,authored,temps,dispatch).disposition==odh::PrimitiveDisposition::unsupported,
                "changed reference generation cannot reuse removal authorization");
            auto wrong=dispatch;++wrong.ticket;
            require(unauthenticated(frame,authored,temps,wrong).disposition==odh::PrimitiveDisposition::unsupported,
                "changed dispatch cannot request removal");
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
        });
    proof.start(key,context(caller,current));
    require(proof.run(20)==odh::VmStatus::returned_true&&admitted.empty(),"forged proofs never reach scheduler admission");
}
}
void native_position_removal_cohort(){
    odh::World world;
    require(world.create(1,odh::EntityKind::object)&&world.create(2,odh::EntityKind::object),"native deletion cohort");
    const odh::WorldEntityReference first{1,world.find(1)->generation},second{2,world.find(2)->generation};
    auto stale=second;++stale.generation;
    require(!odh::mark_native_object_removal_positions(world,{first,stale})&&
        !world.skip_native_recovery_position(first),"whole-cohort preflight rejects stale member before first write");
    require(!odh::mark_native_object_removal_positions(world,{first,first})&&
        !odh::mark_native_object_removal_positions(world,{}),"duplicate and empty native cohort rejected");
    require(world.set_suppress_referenced_reset(first,false)&&world.set_suppress_referenced_reset(second,true),"independent policies");
    require(odh::mark_native_object_removal_positions(world,{first,second})&&
        world.skip_native_recovery_position(first)==std::optional<bool>{true}&&
        world.skip_native_recovery_position(second)==std::optional<bool>{true}&&
        world.suppress_referenced_reset(first)==std::optional<bool>{false}&&
        world.suppress_referenced_reset(second)==std::optional<bool>{true},
        "all members marked together before callback without changing reset suppression");
    require(world.mark_dead(first)&&world.skip_native_recovery_position(second)==std::optional<bool>{true},
        "first member callback observes sibling already marked");
}
void native_policy_publication(){
    odh::World world;require(world.create(1,odh::EntityKind::object)&&world.create(2,odh::EntityKind::object),"policy fixture owners");
    const odh::WorldEntityReference ref{2,world.find(2)->generation};
    require(!world.suppress_referenced_reset(ref),"generic constructor does not invent native recovery policy");
    const auto native=odh::object_removal_primitive(world,[&](const auto& request,const auto&)->std::optional<std::string>{
        if(!odh::publish_native_object_removal_policy(world,request))return "native policy publication failed";
        return {};
    });
    for(const auto flags:{0,1,2,3,128,255}){
        odh::Vm vm(program(instruction(1,static_cast<std::uint8_t>(flags))),world.vm_memory(),{},native);
        vm.start(key,{1,1,2});require(vm.run(20)==odh::VmStatus::returned_true&&
            world.suppress_referenced_reset(ref)==std::optional<bool>{(flags&2)!=0},
            "native admitted Delete publishes only operand flag2 without retiring the body");
    }
    const auto before=world.suppress_referenced_reset(ref);
    auto stale=ref;++stale.generation;
    require(!odh::publish_native_object_removal_policy(world,{stale.id,stale.generation,0,false})&&
        world.suppress_referenced_reset(ref)==before,"stale publication never changes the current body's policy");
    auto denied=odh::object_removal_primitive(world,[](const auto&,const auto&)->std::optional<std::string>{return "host declined";});
    odh::Vm vm(program(instruction(1,0)),world.vm_memory(),{},denied);vm.start(key,{1,1,2});
    require(vm.run(20)==odh::VmStatus::fault&&world.suppress_referenced_reset(ref)==before,
        "rejected deletion never publishes native policy");
    require(world.create(3,odh::EntityKind::object),"legacy removal target created");
    const auto legacy=odh::object_removal_primitive(world,[](const auto&,const auto&)->std::optional<std::string>{return {};});
    odh::Vm legacy_vm(program(instruction(1,2)),world.vm_memory(),{},legacy);legacy_vm.start(key,{1,1,3});
    require(legacy_vm.run(20)==odh::VmStatus::returned_true&&
        !world.suppress_referenced_reset({3,world.find(3)->generation}),
        "legacy host does not gain native policy publication implicitly");
}
int main(){try{bound_reference_removal();native_policy_publication();native_position_removal_cohort();
    odh::World world;
    require(world.create(1,odh::EntityKind::object,{})&&world.create(2,odh::EntityKind::object,{}),"World object fixture");
    std::vector<odh::ObjectRemovalRequest> requests;
    const odh::Vm* origin=nullptr;bool checking=false;
    const auto handler=odh::object_removal_primitive(world,[&](const auto& request,const auto& dispatch)
        ->std::optional<std::string>{
        origin=dispatch.origin;checking=dispatch.check_mode;
        if(dispatch.check_mode)return "removal not admitted from synchronous check";
        requests.push_back(request);return {};
    });
    for(const auto flags:{0,1,2,3,255}) {
        requests.clear();odh::Vm vm(program(instruction(0,static_cast<std::uint8_t>(flags))),{}, {},handler);
        vm.start(key,{1,1,2});
        require(vm.run(20)==odh::VmStatus::next_tick&&vm.steps()==1&&vm.stack()[0].pc==1&&vm.temps()[0]==0,
            "caller removal branches TRUE but cannot execute following instruction in same slice");
        require(requests.size()==1&&requests[0].target==1&&requests[0].generation==world.find(1)->generation&&
            requests[0].flags==flags&&requests[0].caller&&origin==&vm&&!checking,
            "all raw flags including unknown bits reach atomic host admission unchanged");
        require(world.find(1)&&world.find(2)&&world.reserved_entity_ids().empty(),
            "handler queues intent without retiring or reserving World objects");
        require(vm.resume_next_tick()&&vm.run(20)==odh::VmStatus::returned_true&&vm.temps()[0]==77&&requests.size()==1,
            "host release executes TRUE continuation without another removal request");
    }
    for(const auto selector:{1,256,65535}) {
        requests.clear();odh::Vm vm(program(instruction(static_cast<std::uint16_t>(selector),3)),{}, {},handler);
        vm.start(key,{1,1,2});
        require(vm.run(20)==odh::VmStatus::returned_true&&vm.steps()==2&&vm.temps()[0]==77,
            "every nonzero LE16 selector removes StackObject and continues immediately");
        require(requests.size()==1&&requests[0].target==2&&requests[0].generation==world.find(2)->generation&&
            !requests[0].caller,"StackObject request carries its exact generation and noncaller identity");
    }
    requests.clear();
    odh::Vm aliased(program(instruction(1,3)),{}, {},handler);aliased.start(key,{1,1,1});
    require(aliased.run(20)==odh::VmStatus::next_tick&&requests.size()==1&&requests[0].caller,
        "StackObject aliasing Caller still uses TRUE_NEXT_TICK");
    // Repeated requests must retain source branch semantics even when the host
    // has already queued this generation. Deduplication belongs to that host.
    const auto deduplicating=odh::object_removal_primitive(world,[](const auto&,const auto&)
        ->std::optional<std::string>{return {};});
    for(unsigned repeat=0;repeat<2;++repeat) {
        odh::Vm vm(program(instruction()),{}, {},deduplicating);vm.start(key,{1,1,2});
        require(vm.run(20)==odh::VmStatus::next_tick,"idempotently accepted repeated caller request still yields");
    }
    const auto absent_callback=odh::object_removal_primitive(world,{});
    odh::Vm missing_stack(program(instruction(256,255)),{}, {},absent_callback);missing_stack.start(key,{1,1,99});
    require(missing_stack.run(20)==odh::VmStatus::returned_true&&missing_stack.temps()[0]==77,
        "missing StackObject succeeds without requiring callback");
    odh::Vm missing_caller(program(instruction(0,255)),{}, {},absent_callback);missing_caller.start(key,{99,99,2});
    require(missing_caller.run(20)==odh::VmStatus::next_tick&&missing_caller.stack()[0].pc==1,
        "missing Caller target compares the same null pointer and yields");
    odh::Vm no_scheduler(program(instruction()),{}, {},absent_callback);no_scheduler.start(key,{1,1,2});
    require(no_scheduler.run(20)==odh::VmStatus::fault&&no_scheduler.stack()[0].pc==0&&
        no_scheduler.temps()[0]==0&&world.find(1),"live target without scheduler refuses before branch and World effects");
    unsigned admissions=0;
    const auto refusal=odh::object_removal_primitive(world,[&](const auto&,const auto&)
        ->std::optional<std::string>{++admissions;return "unsupported host policy";});
    odh::Vm rejected(program(instruction()),{}, {},refusal);rejected.start(key,{1,1,2});
    require(rejected.run(20)==odh::VmStatus::fault&&rejected.diagnostic()=="unsupported host policy"&&
        rejected.stack()[0].pc==0&&admissions==1&&world.find(1),"callback rejection faults explicitly without taking FALSE or TRUE branch");
    const auto throwing=odh::object_removal_primitive(world,[](const auto&,const auto&)
        ->std::optional<std::string>{throw std::runtime_error("atomic preflight failure");});
    odh::Vm exception(program(instruction()),{}, {},throwing);exception.start(key,{1,1,2});
    require(exception.run(20)==odh::VmStatus::fault&&exception.stack()[0].pc==0&&
        exception.diagnostic().find("atomic preflight failure")!=std::string::npos&&world.find(1),
        "standard callback exception becomes explicit unsupported diagnostic");
    const auto unknown=odh::object_removal_primitive(world,[](const auto&,const auto&)
        ->std::optional<std::string>{throw 7;});
    odh::Vm unknown_exception(program(instruction()),{}, {},unknown);unknown_exception.start(key,{1,1,2});
    require(unknown_exception.run(20)==odh::VmStatus::fault&&world.find(1),"nonstandard exception also fails explicitly");
    const auto before=requests.size();auto other=instruction();other.opcode=17;
    const odh::VmFrame frame{key,0,{1,1,2},{},{}};
    require(handler(frame,other,{}, {nullptr,1}).disposition==odh::PrimitiveDisposition::unsupported&&
        requests.size()==before,"unrelated opcode never calls removal scheduler");
    odh::Vm check(program(instruction()),{}, {},handler);check.start(key,{1,1,2});
    const auto check_result=check.run_synchronous_check(20,handler);
    require(check_result.started&&check_result.status==odh::VmStatus::fault&&checking&&requests.size()==before&&
        check_result.diagnostic=="removal not admitted from synchronous check",
        "dispatch check context reaches host preflight before any queue admission");
    std::cout<<checks<<" object removal checks passed (native VM and World fixtures)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
