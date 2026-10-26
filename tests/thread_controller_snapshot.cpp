#include "odh/entity_thread_controllers.hpp"
#include "odh/entity_thread_controllers_snapshot_codec.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <sstream>

namespace { int allocation_countdown=-1; }
void* operator new(std::size_t size) {
    if(allocation_countdown==0){allocation_countdown=-1;throw std::bad_alloc();}
    if(allocation_countdown>0)--allocation_countdown;
    if(auto* p=std::malloc(size?size:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new(std::size_t size,const std::nothrow_t&) noexcept {
    try{return ::operator new(size);}catch(...){return nullptr;}
}
void operator delete(void* p,const std::nothrow_t&) noexcept {std::free(p);}
namespace {
int checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
odh::RoutineKey key(unsigned id){return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Bhav routine(std::initializer_list<odh::Instruction> code){return {0x8002,code,{},odh::Bhav::Metadata{0,4,4,0}};}
odh::Instruction sleep(unsigned yes=1){return {0,static_cast<std::uint8_t>(yes),255,{}};}
odh::Instruction idle(unsigned allow=1,unsigned yes=1){return {17,static_cast<std::uint8_t>(yes),255,{0,0,static_cast<std::uint8_t>(allow),0,0,0,0,0}};}
odh::Instruction set(unsigned temp,unsigned value,unsigned yes=254){return {2,static_cast<std::uint8_t>(yes),255,{static_cast<std::uint8_t>(temp),0,static_cast<std::uint8_t>(value),0,0,5,8,7}};}
enum class Kind{sleep,idle,actions,notification_chain,nested};
odh::RoutineSet program(Kind kind){
    if(kind==Kind::sleep)return {{key(4096),routine({sleep(),set(2,99)})}};
    if(kind==Kind::notification_chain)return {{key(4096),routine({idle(0),sleep(2),set(2,99)})}};
    return {{key(4096),routine({idle(kind==Kind::actions||kind==Kind::nested?1:0),set(2,99)})}};
}
odh::RoutineOwners owners(){return {{22,{
    {key(4096),routine({sleep(),set(0,22)})},
    {key(4097),routine({set(1,33,255)})},
    {key(4098),routine({{2,1,255,{0,0,1,0,0,5,10,7}},idle(1,2),set(3,77)})},
    {key(4099),routine({sleep(),set(4,88)})},
    {key(4100),routine({sleep(),set(4,88,255)})}}}};}
template<class T>void optional(std::ostream& out,const std::optional<T>& value){out<<bool(value)<<':';if(value)out<<*value;out<<';';}
void context(std::ostream& out,const odh::VmContext& c){out<<c.caller<<','<<c.callee<<','<<c.stack_object<<','<<c.code_owner<<';';}
void trace(std::ostream& out,const odh::VmTrace& t){
    out<<t.step<<','<<t.depth<<','<<int(t.key.scope)<<','<<t.key.id<<','<<t.pc<<','<<t.code_owner<<','
       <<t.instruction.opcode<<','<<int(t.instruction.on_true)<<','<<int(t.instruction.on_false)<<';';
    for(auto v:t.instruction.operands)out<<unsigned(v)<<',';
}
std::string values(const odh::EntityThreadControllersSnapshot& s){
    std::ostringstream out;const auto& a=s.sleep;const auto& q=s.interactions;
    out<<s.thread_interrupt<<';'<<bool(a.actor)<<';';
    if(a.actor)out<<a.actor->id<<','<<a.actor->generation<<','<<a.actor->run_generation<<';';
    out<<bool(a.job)<<';';if(a.job)out<<a.job->ticket<<','<<a.job->idle_start<<','<<a.job->deadline<<','<<a.job->argument<<';';
    optional(out,a.last_tick);out<<a.clock<<','<<a.interrupted<<','<<a.notify_resume<<';'<<a.diagnostic.size()<<':'<<a.diagnostic<<';';
    out<<q.actor_id<<','<<q.capacity<<','<<q.actor_generation<<','<<q.run_generation<<','<<q.sequence<<','<<q.clock<<','<<q.idle_start<<';';
    optional(out,q.last_tick);optional(out,q.resume_tick);out<<int(q.profile)<<','<<q.next_queue_uid<<';'<<q.actions.size()<<';';
    for(const auto& action:q.actions){
        out<<action.uid<<','<<action.target_generation<<','<<int(action.call.key.scope)<<','<<action.call.key.id<<','<<int(action.call.return_mode)<<';';
        context(out,action.call.context);out<<action.call.arguments.size()<<';';for(auto v:action.call.arguments)out<<v<<',';
        out<<action.queue_uid<<','<<action.priority<<','<<int(action.mode)<<','<<action.flags<<','<<action.notify_idle<<','<<action.check_permissions<<';';
        out<<bool(action.check)<<';';if(action.check)out<<int(action.check->scope)<<','<<action.check->id<<';';
    }
    out<<bool(q.active)<<';';if(q.active)out<<q.active->uid<<','<<q.active->ticket<<';';
    out<<q.suspended_active.size()<<';';for(const auto& parent:q.suspended_active)out<<parent.uid<<','<<parent.ticket<<';';
    out<<bool(q.wait)<<';';if(q.wait)out<<q.wait->ticket<<','<<q.wait->deadline<<';';
    out<<bool(q.completion)<<';';if(q.completion)out<<q.completion->uid<<','<<q.completion->ticket<<','<<q.completion->tick<<','<<q.completion->result<<';';
    out<<q.interrupted<<';'<<q.diagnostic.size()<<':'<<q.diagnostic<<';';return out.str();
}
std::string values(const odh::VmSnapshot& s){
    std::ostringstream out;out<<int(s.status)<<','<<s.steps<<','<<s.ticket_sequence<<','<<s.run_generation<<';'
       <<s.diagnostic.size()<<':'<<s.diagnostic<<';';for(auto v:s.temps)out<<v<<',';
    for(auto v:s.temp_xl)out<<v<<',';
    out<<s.stack.size()<<';';for(const auto& frame:s.stack){
        out<<int(frame.key.scope)<<','<<frame.key.id<<','<<frame.pc<<','<<int(frame.return_mode)<<','<<frame.invocation_ticket<<';';
        context(out,frame.context);out<<frame.arguments.size()<<';';for(auto v:frame.arguments)out<<v<<',';
        out<<frame.locals.size()<<';';for(auto v:frame.locals)out<<v<<',';
    }
    const auto pending=[&](const auto& p){out<<p.ticket<<';';trace(out,p.instruction);context(out,p.context);};
    out<<bool(s.pending)<<';';if(s.pending)pending(*s.pending);
    out<<s.suspended_pending.size()<<';';for(const auto& p:s.suspended_pending)pending(p);
    const auto returned=[&](const auto& r){out<<r.ticket<<','<<r.result<<','<<r.code_owner<<','<<r.run_generation<<';';};
    out<<bool(s.interaction_return)<<';';if(s.interaction_return)returned(*s.interaction_return);
    out<<bool(s.external_frame_return)<<';';if(s.external_frame_return)returned(*s.external_frame_return);
    return out.str();
}
odh::EntityThreadControllersSnapshot roundtrip(const odh::EntityThreadControllersSnapshot& state){
    return odh::decode_entity_thread_controllers_snapshot(odh::encode_entity_thread_controllers_snapshot(state));
}
struct Fixture{
    Kind kind;bool has_rng;std::uint64_t rng_cycles=0;
    odh::World world;odh::EntityThreadControllers controllers;odh::Vm vm;
    explicit Fixture(Kind selected,bool initialize=true,bool rng=true):kind(selected),has_rng(rng),
        controllers(world,1,rng?std::function<void()>{[this]{++rng_cycles;}}:std::function<void()>{}),
        vm(program(kind),world.vm_memory(),{},[this](const auto& f,const auto& i,const auto& t,const auto& d){
            if(i.opcode==0)return controllers.sleep_primitive()(f,i,t,d);
            if(i.opcode==17)return controllers.interaction_primitive()(f,i,t,d);
            return odh::PrimitiveOutcome{};
        },owners()){
        if(!initialize)return;
        require(world.create(1,odh::EntityKind::avatar,kind==Kind::nested?odh::EntityLayout{0,80,0,16}:odh::EntityLayout{})&&world.create(2,odh::EntityKind::object)&&
                world.declare_object_word(1,8,16),"World fixture");
        vm.start(key(4096),{1,1,1,0},{kind==Kind::actions||kind==Kind::nested?std::int16_t{0}:std::int16_t{6}});
        require(controllers.bind(vm),"bind fixture controllers after VM initialization");
    }
    bool enqueue(unsigned action=4096){return controllers.interactions().enqueue(key(action),{1,2,2,22},{3,91,-7});}
    std::string run(std::uint64_t budget){
        std::ostringstream out;vm.run(budget,[&](const auto& t){trace(out,t);});
        require(controllers.observe(),"observe queue after VM run");return out.str();
    }
    std::string step(std::uint64_t tick,std::uint64_t budget){
        require(controllers.advance(tick),"advance restored clocks consecutively");return run(budget);
    }
};
std::unique_ptr<Fixture> destination(const Fixture& source){
    auto result=std::make_unique<Fixture>(source.kind,false,source.has_rng);
    require(result->world.restore_snapshot(source.world.snapshot()),"restore surrounding World first");
    require(result->vm.restore_snapshot(odh::decode_vm_snapshot(odh::encode_vm_snapshot(source.vm.snapshot()))),"restore bound VM execution before services");
    require(result->controllers.bind(result->vm),"bind fresh destination services");
    result->rng_cycles=source.rng_cycles;return result;
}
void same(const Fixture& a,const Fixture& b){
    require(values(a.vm.snapshot())==values(b.vm.snapshot()),"complete VM snapshots match");
    require(values(a.controllers.snapshot())==values(b.controllers.snapshot()),"all controller values match");
    require(a.rng_cycles==b.rng_cycles,"external RNG callback counts match");
    const auto x=a.world.snapshot(),y=b.world.snapshot();
    require(x.word_count==y.word_count&&x.generation_sequence==y.generation_sequence&&x.spatial_revision==y.spatial_revision&&
            x.entities.size()==y.entities.size()&&x.entities[0].object_words==y.entities[0].object_words&&
            x.entities[0].person_data==y.entities[0].person_data,
            "fixture World flags and accounting match");
}
std::unique_ptr<Fixture> restored(const Fixture& source,bool composed=true){
    auto result=destination(source);const auto snapshot=roundtrip(source.controllers.snapshot());
    if(composed)require(result->controllers.restore_snapshot(snapshot),"restore combined controller state");
    else require(result->controllers.sleep().restore_snapshot(snapshot.sleep)&&
                 result->controllers.interactions().restore_snapshot(snapshot.interactions),"restore standalone services");
    same(source,*result);return result;
}
void advance_same(Fixture& a,Fixture& b,std::uint64_t tick,std::uint64_t budget){
    const auto left=a.step(tick,budget),right=b.step(tick,budget);
    require(left==right,"continuous and restored instruction traces match");same(a,b);
}
void wait_and_notifications(){
    Fixture source(Kind::sleep);source.run(1);auto copy=restored(source,false);
    for(std::uint64_t tick=1;tick<=16;++tick)advance_same(source,*copy,tick,8);
    require(source.vm.status()==odh::VmStatus::returned_true&&source.rng_cycles==1,"natural Sleep completes once with one RNG cycle");
    Fixture sleeping(Kind::sleep);sleeping.run(1);sleeping.step(1,1);
    require(sleeping.controllers.notify()&&sleeping.controllers.thread_interrupted(),"Sleep Notify retains shared interrupt before reexecution");
    auto notified=restored(sleeping);
    require(sleeping.run(8)==notified->run(8),"notified Sleep reexecutes identically");same(sleeping,*notified);
    require(!sleeping.controllers.thread_interrupted()&&sleeping.rng_cycles==0,"Notify consumes latch without natural RNG completion");
    Fixture idling(Kind::idle);idling.run(1);idling.step(1,1);auto idle_copy=restored(idling);
    advance_same(idling,*idle_copy,2,1);
    require(idling.controllers.notify()&&idle_copy->controllers.notify(),"IdleForInput Notify releases both pending tickets");
    auto idle_notified=restored(idling);
    require(idling.run(8)==idle_notified->run(8),"notified idle resumes matching branch");same(idling,*idle_notified);
    Fixture interrupted(Kind::idle);interrupted.run(1);
    require(interrupted.controllers.interactions().interrupt(),"standalone idle interrupt recorded");
    auto interrupt_copy=restored(interrupted);advance_same(interrupted,*interrupt_copy,1,8);
    Fixture pre_sleep(Kind::sleep);
    require(pre_sleep.controllers.sleep().interrupt(),"Sleep interrupt before first wait records actor and local signal");
    auto pre_copy=restored(pre_sleep);require(pre_sleep.run(8)==pre_copy->run(8),"pre-wait Sleep interrupt survives restore");same(pre_sleep,*pre_copy);
    Fixture chain(Kind::notification_chain);require(chain.enqueue()&&chain.controllers.notify(),"queued nonpush idle receives thread notification before dispatch");
    auto chain_copy=restored(chain);require(chain.run(8)==chain_copy->run(8),"queued idle preserves signal for following Sleep");same(chain,*chain_copy);
    require(!chain.controllers.thread_interrupted()&&chain.controllers.interactions().size()==1&&chain.vm.steps()==3,
            "shared signal survives bypassed idle and is consumed by Sleep");
    auto terminal_copy=restored(chain);same(chain,*terminal_copy);
    Fixture no_rng(Kind::sleep,true,false);no_rng.run(1);
    for(std::uint64_t tick=1;tick<=16;++tick)no_rng.step(tick,8);
    require(!no_rng.controllers.sleep().diagnostic().empty(),"missing RNG callback produces retained diagnostic");
    auto diagnostic_copy=restored(no_rng);same(no_rng,*diagnostic_copy);
}
void actions_and_boundaries(){
    Fixture source(Kind::actions);require(source.enqueue()&&source.enqueue(4097),"two FIFO actions with explicit args");
    const auto queued_before=values(source.controllers.snapshot());auto independent_queue=source.controllers.snapshot();
    independent_queue.interactions.actions.front().call.arguments[0]=999;
    require(values(source.controllers.snapshot())==queued_before,"snapshot FIFO arguments do not alias controller storage");
    auto queued=restored(source);require(source.run(1)==queued->run(1),"queued-only checkpoint dispatches same action");same(source,*queued);
    bool active_wait=false,yielded=false,complete=false;
    for(std::uint64_t tick=1;tick<=24;++tick){
        auto copy=restored(source,tick%2==1);
        const auto state=source.controllers.snapshot();
        active_wait=active_wait||(state.interactions.active&&state.sleep.job);
        yielded=yielded||state.interactions.resume_tick.has_value();
        complete=complete||(state.interactions.completion&&!state.interactions.resume_tick&&!state.interactions.active);
        advance_same(source,*copy,tick,1);
    }
    require(active_wait&&yielded&&complete&&source.vm.status()==odh::VmStatus::returned_true&&
            source.vm.temps()[0]==22&&source.vm.temps()[1]==33&&source.vm.temps()[2]==99&&source.rng_cycles==1,
            "active action WAIT, false result, next-tick resume and retained completion all continue");
    const auto before=source.controllers.snapshot();
    require(!source.controllers.restore_snapshot(before)&&!source.controllers.sleep().restore_snapshot(before.sleep)&&
            !source.controllers.interactions().restore_snapshot(before.interactions),"used destinations reject restore");
    auto independent=before;independent.interactions.diagnostic="independent";independent.thread_interrupt=true;
    require(values(before)==values(source.controllers.snapshot()),"snapshot diagnostic and latch copies are independent");
    Fixture unobserved(Kind::actions);require(unobserved.enqueue(4097),"unobserved completion fixture");
    require(unobserved.vm.run(8)==odh::VmStatus::yielded&&unobserved.controllers.interactions().active(),
            "action return remains active until host observes it");
    auto unobserved_copy=restored(unobserved);
    require(unobserved.controllers.observe()&&unobserved_copy->controllers.observe(),"restored unobserved return removes the same FIFO front");
    same(unobserved,*unobserved_copy);advance_same(unobserved,*unobserved_copy,1,8);
}
void nested_save_continuation(){
    for(const bool child_result:{true,false}){
        Fixture source(Kind::nested);
        require(source.world.set_person_data(1,33,0)&&source.world.set_person_data(1,71,1),"nested priority and noninterruptable state");
        auto& queue=source.controllers.interactions();
        require(queue.enqueue_ts1(key(4098),{1,2,2,22},{4},10,
            odh::InteractionQueueMode::normal,0,false,false),"enqueue parent action");
        source.run(1);
        require(queue.active(),"parent dispatched before child insertion");
        require(queue.enqueue_ts1(key(child_result?4099:4100),{1,2,2,22},{2},20,
            odh::InteractionQueueMode::normal,0,false,false),"enqueue higher-priority child");
        source.run(2);
        require(queue.snapshot().suspended_active.size()==1&&queue.size()==2&&
            source.vm.stack()[1].context.stack_object==1&&queue.snapshot().actions[0].call.context.stack_object==2,
            "AllowPush protects the parent including its changed StackObjectID");
        source.vm.temp_xl()[0]=0x12345678;
        source.run(1);
        require(source.controllers.sleep().snapshot().job.has_value(),"child is suspended in Sleep");
        auto legacy_state=source.controllers.snapshot();legacy_state.interactions.profile=odh::InteractionQueueProfile::legacy_fifo;
        auto invalid_legacy=destination(source);
        require(!invalid_legacy->controllers.restore_snapshot(legacy_state),"legacy profile cannot acquire a fabricated active prefix");
        std::vector<std::unique_ptr<Fixture>> copies;
        copies.push_back(restored(source));
        bool resumed_parent=false;
        for(std::uint64_t tick=1;tick<=16;++tick){
            require(source.controllers.advance(tick),"advance nested source");
            auto after_advance=restored(source);
            std::ostringstream instruction_trace;
            source.vm.run(8,[&](const auto& value){trace(instruction_trace,value);});
            std::unique_ptr<Fixture> before_observe;
            if(source.vm.interaction_return()&&
               !source.controllers.snapshot().interactions.suspended_active.empty())
                before_observe=restored(source);
            require(source.controllers.observe(),"observe nested child return");
            const auto expected=instruction_trace.str();
            for(auto& copy:copies){
                require(copy->step(tick,8)==expected,"nested restored trace matches uninterrupted trace");
                same(source,*copy);
            }
            require(after_advance->run(8)==expected,"restore between clock advance and VM run preserves execution");
            same(source,*after_advance);copies.push_back(std::move(after_advance));
            if(before_observe){
                require(before_observe->controllers.observe(),"observe child return restored before controller observation");
                same(source,*before_observe);
                copies.push_back(std::move(before_observe));
            }
            const auto state=source.controllers.snapshot();
            if(state.interactions.resume_tick&&state.interactions.active){
                require(state.interactions.suspended_active.empty()&&
                    state.interactions.completion&&state.interactions.completion->result==child_result&&
                    source.world.find(1)->person_data[33]==10&&source.world.find(1)->person_data[71]==1,
                    "child result preserves parent and restores its priority before next tick");
                resumed_parent=true;
            }
            // Every cut restores both codecs/controllers, including the gap
            // between child completion and the parent's next-tick resumption.
            copies.push_back(restored(source));
        }
        require(resumed_parent&&source.vm.temps()[3]==77&&source.vm.temps()[4]==88&&
            source.vm.temp_xl()[0]==0x12345678&&!queue.active()&&queue.size()==0&&
            source.world.find(1)->person_data[33]==0&&source.world.find(1)->person_data[71]==0,
            "true and false children both return control to their parent without losing registers");
    }
}
void cancelled_action_continuation(){
    Fixture source(Kind::nested);
    auto& queue=source.controllers.interactions();
    require(queue.enqueue_ts1(key(4098),{1,2,2,22},{4},50,
        odh::InteractionQueueMode::normal,0,false,false),"cancellation continuation enqueues parent");
    const auto uid=queue.last_enqueued_uid();source.run(1);
    require(queue.active()&&queue.cancel(uid)==odh::InteractionCancelResult::notified&&
        queue.contains_uid(uid)&&queue.snapshot().actions.front().notify_idle&&
        source.world.find(1)->person_data[33]==0&&
        (source.world.find(1)->object_words.at(8)&128),"cancel active action retains its frame and publishes game cancel signal");
    auto copy=restored(source);
    require(source.run(8)==copy->run(8),"cancelled action resumes its game cleanup identically after reopen");same(source,*copy);
    for(std::uint64_t tick=1;tick<=6;++tick){
        auto at_cut=restored(source);
        const auto expected=source.step(tick,8);
        require(copy->step(tick,8)==expected&&at_cut->step(tick,8)==expected,
            "cancelled action and Main dispatch order matches after repeated reopening");
        same(source,*copy);same(source,*at_cut);
    }
    require(!queue.active()&&!queue.contains_uid(uid)&&queue.completion()&&
        queue.completion()->uid==uid&&source.vm.temps()[3]==77&&
        !(source.world.find(1)->object_words.at(8)&128),
        "cancelled action completes through BHAV and clears the original cancellation flag");
}
void deferred_removal_keeps_cleanup_data(){
    odh::World original,copy;
    require(original.create(1,odh::EntityKind::avatar)&&original.create(2,odh::EntityKind::object)&&
        original.declare_object_word(2,8,80),"deferred cleanup object has original flag bits");
    const odh::RoutineSet code{{key(4096),routine({
        {18,1,255,{}},
        {2,2,255,{5,0,8,0,0,5,8,4}}, // Read removed StackObject flags after Remove.
        {2,3,255,{0,0,1,0,0,5,10,7}}, // Release the foreign StackObject reference.
        set(6,99)})}};
    unsigned remove_calls=0;
    const odh::VmPrimitive remove=[&](const auto&,const auto& instruction,const auto&,const auto&){
        if(instruction.opcode!=18)return odh::PrimitiveOutcome{};
        ++remove_calls;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
    };
    odh::Vm source(code,original.vm_memory(),{},remove);
    source.start(key(4096),{1,1,2,0});
    require(source.run(1)==odh::VmStatus::budget_exhausted&&remove_calls==1&&
        source.references_entity(2)&&original.find(2),
        "instruction-budget cut after removal request retains data needed by cleanup");
    require(copy.restore_snapshot(original.snapshot()),"restore World including deferred cleanup data");
    odh::Vm restored_vm(code,copy.vm_memory(),{},remove);
    require(restored_vm.restore_snapshot(odh::decode_vm_snapshot(odh::encode_vm_snapshot(source.snapshot())))&&
        restored_vm.references_entity(2),"deferred object reference survives VM save/reopen");
    require(source.run(8)==odh::VmStatus::returned_true&&restored_vm.run(8)==odh::VmStatus::returned_true&&
        source.temps()[5]==80&&restored_vm.temps()==source.temps()&&remove_calls==1,
        "cleanup reads original flags after Remove without replaying the removal request");
    require(!source.references_entity(2)&&!restored_vm.references_entity(2)&&
        original.remove(2)&&copy.remove(2)&&!original.find(2)&&!copy.find(2),
        "removal becomes safe once foreign frames release their object reference");
}
void rejection_and_atomicity(){
    Fixture source(Kind::actions);require(source.enqueue()&&source.enqueue(4097),"validation actions");source.run(1);source.run(1);
    const auto snapshot=roundtrip(source.controllers.snapshot());require(snapshot.sleep.job&&snapshot.interactions.active,"joint validation fixture contains active action and Sleep wait");
    const auto reject=[&](const odh::EntityThreadControllersSnapshot& bad){
        auto target=destination(source);const auto before=values(target->controllers.snapshot());
        require(!target->controllers.restore_snapshot(bad)&&values(target->controllers.snapshot())==before,
                "invalid combined snapshot leaves both services and latch unchanged");
        require(target->controllers.restore_snapshot(snapshot),"failed restore leaves destination fresh");
    };
    auto bad=snapshot;bad.interactions.capacity=0;reject(bad);
    bad=snapshot;bad.interactions.capacity=33;reject(bad);
    bad=snapshot;bad.interactions.capacity=1;reject(bad);
    bad=snapshot;bad.interactions.actor_id=2;reject(bad);
    bad=snapshot;++bad.interactions.actor_generation;reject(bad);
    bad=snapshot;++bad.interactions.run_generation;reject(bad);
    bad=snapshot;bad.interactions.actions[1].uid=bad.interactions.actions[0].uid;reject(bad);
    bad=snapshot;bad.interactions.sequence=0;reject(bad);
    bad=snapshot;++bad.interactions.actions[0].target_generation;reject(bad);
    bad=snapshot;bad.interactions.actions[0].call.context.stack_object=1;reject(bad);
    bad=snapshot;bad.interactions.actions[0].call.arguments.resize(4097);reject(bad);
    bad=snapshot;bad.interactions.actions[0].call.return_mode=odh::VmCallReturnMode::ordinary;reject(bad);
    bad=snapshot;bad.interactions.actions[0].call.key.scope=static_cast<odh::RoutineScope>(99);reject(bad);
    bad=snapshot;++bad.interactions.active->ticket;reject(bad);
    bad=snapshot;++bad.interactions.active->uid;reject(bad);
    bad=snapshot;bad.interactions.clock=std::numeric_limits<std::uint64_t>::max();reject(bad);
    bad=snapshot;bad.sleep.actor.reset();reject(bad);
    bad=snapshot;++bad.sleep.actor->generation;reject(bad);
    bad=snapshot;++bad.sleep.actor->run_generation;reject(bad);
    bad=snapshot;bad.sleep.actor->id=2;reject(bad);
    bad=snapshot;++bad.sleep.job->ticket;reject(bad);
    bad=snapshot;bad.sleep.job->argument=4;reject(bad);
    bad=snapshot;bad.sleep.job->deadline=0;reject(bad);
    bad=snapshot;++bad.sleep.job->deadline;reject(bad);
    bad=snapshot;bad.sleep.last_tick=1;reject(bad);
    bad=snapshot;bad.sleep.clock=std::numeric_limits<std::uint64_t>::max();reject(bad);
    bad=snapshot;bad.sleep.notify_resume=true;reject(bad);
    bad=snapshot;bad.sleep.job.reset();reject(bad);
    auto target=destination(source);const auto before=values(target->controllers.snapshot());
    auto payload=snapshot;payload.sleep.diagnostic=std::string(128,'s');payload.interactions.diagnostic=std::string(128,'q');
    payload=roundtrip(payload);
    bool published=false;int failures=0;
    for(int fail=0;fail<256;++fail){
        allocation_countdown=fail;bool threw=false;
        try{published=target->controllers.restore_snapshot(payload);}catch(const std::bad_alloc&){threw=true;}
        allocation_countdown=-1;if(published)break;++failures;
        require(threw&&values(target->controllers.snapshot())==before,"every allocation failure rolls back both services and preserves freshness");
    }
    require(failures>4&&published&&values(target->controllers.snapshot())==values(payload),"staged copies eventually publish all services atomically");
    Fixture idle_source(Kind::idle);idle_source.run(1);const auto idle_state=idle_source.controllers.snapshot();
    const auto reject_idle=[&](const odh::EntityThreadControllersSnapshot& state){
        auto idle_target=destination(idle_source);const auto initial=values(idle_target->controllers.snapshot());
        require(!idle_target->controllers.restore_snapshot(state)&&values(idle_target->controllers.snapshot())==initial,"invalid idle wait rejected atomically");
    };
    bad=idle_state;++bad.interactions.wait->ticket;reject_idle(bad);
    bad=idle_state;++bad.interactions.wait->deadline;reject_idle(bad);
    bad=idle_state;bad.interactions.wait.reset();reject_idle(bad);
    bad=idle_state;bad.interactions.idle_start=1;reject_idle(bad);
    Fixture boundary(Kind::actions);require(boundary.enqueue(4097),"completion fixture action");boundary.run(8);
    const auto completion=boundary.controllers.snapshot();require(completion.interactions.resume_tick.has_value(),"completion remains yielded until next tick");
    const auto reject_completion=[&](const odh::EntityThreadControllersSnapshot& state){
        auto completion_target=destination(boundary);const auto initial=values(completion_target->controllers.snapshot());
        require(!completion_target->controllers.restore_snapshot(state)&&values(completion_target->controllers.snapshot())==initial,"invalid completion boundary rejected atomically");
    };
    bad=completion;++bad.interactions.completion->ticket;reject_completion(bad);
    bad=completion;bad.interactions.completion->result=!bad.interactions.completion->result;reject_completion(bad);
    bad=completion;++*bad.interactions.resume_tick;reject_completion(bad);
    bad=completion;bad.interactions.completion->uid=0;reject_completion(bad);
    auto pristine=destination(idle_source);const auto initial=pristine->controllers.snapshot();
    require(!pristine->controllers.restore_snapshot(initial),"missing service for VM pending instruction rejected");
    Fixture untouched(Kind::idle);auto untouched_copy=restored(untouched);
    require(!untouched_copy->controllers.restore_snapshot(untouched.controllers.snapshot()),"even empty service restore consumes freshness");
    bool capture_guard=false,restore_guard=false;
    Fixture guarded(Kind::sleep);const auto safe=guarded.controllers.snapshot();
    guarded.vm.run(1,[&](const auto&){
        try{guarded.controllers.snapshot();}catch(const std::logic_error&){capture_guard=true;}
        try{guarded.controllers.restore_snapshot(safe);}catch(const std::logic_error&){restore_guard=true;}
    });
    require(capture_guard&&restore_guard,"VM callbacks cannot capture or restore in-flight controller values");
}
}
int main(){
    try{wait_and_notifications();actions_and_boundaries();nested_save_continuation();cancelled_action_continuation();deferred_removal_keeps_cleanup_data();rejection_and_atomicity();
        std::cout<<"thread_controller_snapshot: "<<checks<<" checks passed (synthetic in-memory continuation only)\n";}
    catch(const std::exception& error){allocation_countdown=-1;std::cerr<<"thread_controller_snapshot: "<<error.what()<<'\n';return 1;}
}
