#include "odh/action_query.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
odh::RoutineKey key(unsigned n){return odh::routine_key(static_cast<std::uint16_t>(n));}
odh::Bhav tree(std::initializer_list<odh::Instruction> code){return {0x8002,code,{},odh::Bhav::Metadata{0,4,4,0}};}
odh::Instruction query(std::uint8_t yes=254,std::uint8_t no=255){return {37,yes,no,{255,254,253,252,251,250,249,248}};}
odh::Instruction idle(){return {17,1,255,{0,0,1,0,0,0,0,0}};}
struct Graph {
    odh::World world;
    odh::InteractionQueue queue{world,1,8};
    std::unique_ptr<odh::Vm> vm;
    odh::VmPrimitive handler;
    std::optional<odh::PrimitiveOutcome> answer;
    bool disposable=false,missing=false;
    odh::VmStatus check_result=odh::VmStatus::ready;
    Graph(bool nested=false,bool use_check=false,bool reassign=false):disposable(use_check){
        require(world.create(1,odh::EntityKind::avatar,{0,88,3,16})&&world.create(2,odh::EntityKind::object)&&
            world.create(3,odh::EntityKind::object)&&world.declare_object_word(1,8,16),"query identities");
        handler=odh::test_sim_interacting_with_primitive(world,[&](auto id,auto generation)->std::optional<odh::ActionQueryThread>{
            const auto* actor=world.find(id);if(missing||!actor||actor->generation!=generation)return std::nullopt;
            return odh::ActionQueryThread{vm.get(),&queue};
        });
        odh::RoutineSet routines{{key(4096),tree({idle(),{99,254,255,{}}})},
            {key(4100),reassign?tree({{2,1,255,{0,0,2,0,0,5,10,7}},query()}):
                nested?tree({{4101,254,255,{}}}):tree({disposable?odh::Instruction{98,254,255,{}}:query()})},
            {key(4101),tree({disposable?odh::Instruction{98,254,255,{}}:query()})},
            {key(4102),tree({query()})},{key(4110),tree({query(255,254)})}};
        vm=std::make_unique<odh::Vm>(std::move(routines),world.vm_memory(),odh::VmLimits{},
            [&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
                if(instruction.opcode==17)return queue.primitive()(frame,instruction,temps,dispatch);
                if(instruction.opcode==37){answer=handler(frame,instruction,temps,dispatch);return *answer;}
                if(instruction.opcode==98){
                    auto program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4120),tree({query()})}});
                    odh::Vm scratch(program,odh::ShareVmProgramTag{},world.vm_memory(),{},handler);
                    require(scratch.start(key(4120),frame.context,{0,0,0,0},true)==odh::VmStatus::running,"disposable query start");
                    const auto result=scratch.run_synchronous_check_with_registers(*vm,true,100,handler);
                    check_result=result.status;
                    return {result.status==odh::VmStatus::returned_true?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};
                }
                return {odh::PrimitiveDisposition::goto_true,{}};
            });
        require(queue.bind(*vm),"bind authoritative query queue");
    }
    void start(){require(vm->start(key(4096),{1,1,1,0},{10,0,0,0})==odh::VmStatus::running,"start Main");}
    void enqueue(std::uint16_t action=4100,std::int16_t target=2,std::optional<odh::RoutineKey> check={}){
        require(queue.enqueue_source_user(key(action),{1,target,target,0},std::vector<std::int16_t>(4,0),0,check,0),"enqueue actual action");
    }
    void enter(){start();enqueue();require(vm->run(1)==odh::VmStatus::budget_exhausted&&queue.active(),"enter queued action");}
};
void cases(){
    Graph empty;require(empty.queue.interacting_with(empty.world,*empty.vm,1,2)==std::optional<bool>{false},"empty live thread short-circuits false");
    Graph main;main.start();
    require(main.queue.interacting_with(main.world,*main.vm,1,2)==std::optional<bool>{false},"Main is not ActionTree");
    require(!main.queue.interacting_with(main.world,*main.vm,2,2),"wrong Caller cannot borrow queue");
    odh::World other;require(!main.queue.interacting_with(other,*main.vm,1,2),"another World cannot borrow queue");
    for(const auto nested:{false,true}){
        Graph active(nested);active.enter();if(nested)require(active.vm->run(1)==odh::VmStatus::budget_exhausted,"enter ordinary child");
        require(active.queue.interacting_with(active.world,*active.vm,1,2)==std::optional<bool>{true}&&
            active.queue.interacting_with(active.world,*active.vm,1,3)==std::optional<bool>{false}&&
            active.queue.interacting_with(active.world,*active.vm,1,0)==std::optional<bool>{false},"live action and ordinary descendants use ActiveAction.Callee");
        const auto steps=active.vm->steps(),revision=active.world.spatial_revision();
        require(active.vm->run(1)==odh::VmStatus::yielded&&active.answer&&active.answer->disposition==odh::PrimitiveDisposition::goto_true&&
            active.world.spatial_revision()==revision&&active.vm->steps()==steps+1,"authenticated query ignores all operands and has no World effects");
    }
    for(const auto nested:{false,true}){
        Graph source(nested,true);source.enter();if(nested)source.vm->run(1);
        source.vm->run(1);require(source.check_result==odh::VmStatus::returned_true,"disposable check queries persistent live action, not its check stack");
    }
    Graph checked;checked.start();checked.queue.set_check_primitive(checked.handler);checked.enqueue(4100,2,key(4110));
    require(checked.vm->run(1)==odh::VmStatus::budget_exhausted&&checked.queue.active(),
        "read-only query is valid while queue executes candidate CheckAction on Main");
    Graph immediate;immediate.enter();
    require(immediate.queue.enqueue_ts1(key(4102),{1,3,3,0},{0,0,0,0},1,
        odh::InteractionQueueMode::idle,4,false,false)&&immediate.queue.try_run_immediate(),
        "inject real immediate action over protected ordinary action");
    require(immediate.queue.interacting_with(immediate.world,*immediate.vm,1,3)==std::optional<bool>{true}&&
        immediate.queue.interacting_with(immediate.world,*immediate.vm,1,2)==std::optional<bool>{false},
        "deepest immediate ActiveAction owns the queried Callee, not its suspended parent");
    require(immediate.vm->run(1)==odh::VmStatus::yielded&&immediate.vm->immediate_return()&&
        immediate.queue.observe()&&immediate.queue.interacting_with(immediate.world,*immediate.vm,1,2)==std::optional<bool>{true},
        "acknowledged immediate return restores the parent's live query ancestry");
    for(const bool nil_first:{false,true}){
        Graph generations(false,false,true);generations.enter();const auto old=generations.world.find(2)->generation;
        require(generations.world.remove_retaining_body({2,old}),"retain the old accepted Callee while its raw number becomes free");
        if(!nil_first)require(generations.world.create(2,odh::EntityKind::object),"create replacement before authored numeric StackObject assignment");
        require(generations.vm->run(1)==odh::VmStatus::budget_exhausted,"execute genuine authored StackObjectID assignment in the accepted action");
        if(nil_first)require(generations.world.create(2,odh::EntityKind::object)&&!generations.vm->stack().back().context.stack_object_generation,
            "positive raw StackObjectID remains known nil when replacement appears later");
        require(generations.vm->run(1)==odh::VmStatus::yielded&&generations.answer&&
            generations.answer->disposition==odh::PrimitiveDisposition::goto_false,
            "actual source query compares Callee and StackObject reference generations, including explicit nil");
    }
    Graph wrong;wrong.enter();Graph unrelated;require(!wrong.queue.interacting_with(wrong.world,*unrelated.vm,1,2),"wrong real thread is unsupported");
    Graph recycled;recycled.enter();require(recycled.world.remove(2)&&recycled.world.create(2,odh::EntityKind::object)&&
        !recycled.queue.interacting_with(recycled.world,*recycled.vm,1,2),"recycled active target cannot become a positive match");
    Graph missing;missing.missing=true;missing.enter();require(missing.vm->run(1)==odh::VmStatus::fault&&missing.answer&&
        missing.answer->disposition==odh::PrimitiveDisposition::unsupported,"unknown persistent thread is not source false");
    odh::VmFrame frame;odh::Instruction op=query();odh::Ts1Temps temps{};
    require(main.handler(frame,op,temps,{}).disposition==odh::PrimitiveDisposition::unsupported,"unproven dispatch cannot query thread state");
}
void restore_case(){
    Graph source(true);source.enter();source.vm->run(1);
    const auto world=source.world.snapshot();const auto vm=source.vm->snapshot();const auto queue=source.queue.snapshot();
    odh::World restored;require(restored.restore_snapshot(world),"fresh query World restore");
    odh::InteractionQueue actions(restored,1,8);
    std::unique_ptr<odh::Vm> thread;
    const auto handler=odh::test_sim_interacting_with_primitive(restored,[&](auto,auto)->std::optional<odh::ActionQueryThread>{return odh::ActionQueryThread{thread.get(),&actions};});
    thread=std::make_unique<odh::Vm>(source.vm->shared_program_for_owner(0),odh::ShareVmProgramTag{},restored.vm_memory(),odh::VmLimits{},handler);
    require(actions.bind(*thread)&&thread->restore_snapshot(vm)&&actions.restore_snapshot(queue),"restore queue and nested action VM");
    require(actions.interacting_with(restored,*thread,1,2)==std::optional<bool>{true}&&thread->run(1)==odh::VmStatus::yielded,
        "fresh restored nested action answers the same query without synthetic token or VM clone");
}
}
int main(){try{cases();restore_case();std::cout<<"shared live action query passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
