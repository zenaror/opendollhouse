#include "odh/interaction.hpp"
#include "odh/autonomy_check.hpp"
#include "odh/ts1_budget.hpp"
#include "odh/entity_thread_controllers_snapshot_codec.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <memory>
namespace {
int checks=0;
void require(bool v,const char* text){++checks;if(!v)throw std::runtime_error(text);}
odh::RoutineKey key(unsigned id){return odh::routine_key(static_cast<std::uint16_t>(id));}
odh::Instruction idle(unsigned allow=1,unsigned yes=254){return {17,static_cast<std::uint8_t>(yes),255,{0,0,static_cast<std::uint8_t>(allow),0,0,0,0,0}};}
odh::Instruction set(unsigned temp,unsigned value,unsigned yes=254){return {2,static_cast<std::uint8_t>(yes),255,{static_cast<std::uint8_t>(temp),0,static_cast<std::uint8_t>(value),0,0,5,8,7}};}
odh::Bhav bhav(std::initializer_list<odh::Instruction> i){return {0x8002,i,{},odh::Bhav::Metadata{0,4,4,0}};}
struct Fixture {
 odh::World world;odh::InteractionQueue queue;odh::Vm vm;
 Fixture(unsigned allow=1,std::size_t capacity=2,bool flags=true):queue(world,1,capacity),
 vm({{key(4096),bhav({idle(allow,1),set(2,99)})}},world.vm_memory(),{},queue.primitive(),
    {{22,{{key(4096),bhav({set(0,22)})},{key(4097),bhav({set(1,33,255)})}}}}) {
  world.create(1,odh::EntityKind::avatar,{0,0,0,16});world.create(2,odh::EntityKind::object);
  if(flags)world.declare_object_word(1,8,16);
  require(queue.bind(vm),"bind registered avatar");vm.start(key(4096),{1,1,1,0},{0});
 }
 bool enqueue(unsigned id=4096){return queue.enqueue(key(id),{1,2,2,22});}
};
struct CancelFixture {
 odh::World world;odh::InteractionQueue queue;odh::Vm vm;
 CancelFixture():queue(world,1,8),
  vm({{key(4096),bhav({idle(1,1),set(2,99)})}},world.vm_memory(),{},queue.primitive(),
     {{22,{{key(4097),bhav({set(0,33)})},{key(4098),bhav({set(1,44)})}}}}) {
  world.create(1,odh::EntityKind::avatar,{0,70,0,0});world.create(2,odh::EntityKind::object);
  world.declare_object_word(1,8,16);vm.start(key(4096),{1,1,1,0},{4});queue.bind(vm);
 }
 bool enqueue(unsigned id,std::int16_t priority,odh::InteractionQueueMode mode=odh::InteractionQueueMode::normal,
              std::uint32_t flags=0,bool push_tail=false) {
  return queue.enqueue_ts1(key(id),{1,2,2,22},{},priority,mode,flags,false,push_tail);
 }
};
}
namespace {
void functional_icon_tests(){
 struct Graph {
  odh::World world;odh::InteractionQueue queue{world,1,8};std::unique_ptr<odh::Vm> vm;
  std::optional<bool> tree;bool empty=false,stale_icon=false,stale_epoch=false,committed=false,nested=false,external=false;
  std::optional<odh::InteractionQueue::PreparedIconUpdate> parked;
  std::optional<odh::InteractionQueue::PreparedIconUpdate> replay;
  bool repeat_seed=false,seeded=false;
  static odh::RoutineSet main(){return {{key(4096),bhav({{20,254,255,{0,0,1,0,0,0,0,0}}})},
      {key(4097),bhav({idle(1,1),set(7,91)})}};}
  static odh::RoutineSet actions(bool nested=false,bool external=false){
   const odh::Instruction icon{20,254,255,{0,0,1,0,0,0,0,0}};
   return {{key(4100),bhav({{2,1,255,{0,0,3,0,0,5,10,7}},
       external?odh::Instruction{99,254,255,{}}:nested?odh::Instruction{4101,254,255,{}}:icon})},
       {key(4101),bhav({icon})},{key(4102),bhav({set(6,77)})}};
  }
  explicit Graph(bool nested_child=false,bool external_child=false):nested(nested_child),external(external_child){
   require(world.create(1,odh::EntityKind::avatar,{0,88})&&world.create(2,odh::EntityKind::object)&&
       world.create(3,odh::EntityKind::object)&&world.declare_object_word(1,8,16),"icon graph identities");
   vm=std::make_unique<odh::Vm>(main(),world.vm_memory(),odh::VmLimits{},
       [this](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
    if(instruction.opcode==99)return {odh::PrimitiveDisposition::wait,{}};
    if(instruction.opcode!=20)return queue.primitive()(frame,instruction,temps,dispatch);
    tree=queue.action_tree_for(frame,instruction,dispatch);
    if(!tree)return {odh::PrimitiveDisposition::unsupported,"unproved ActionTree"};
    if(!*tree)return {odh::PrimitiveDisposition::goto_true,{}};
    if(repeat_seed&&!seeded){
      auto seed=queue.prepare_icon_owner_update(frame,instruction,dispatch,3,world.find(3)->generation);
      require(seed&&seed->commit(),"seed source icon with exact empty-action repeat");seeded=true;
      return {odh::PrimitiveDisposition::continue_execution,{}};
    }
    const odh::VmCall child{key(4102),{1,3,3,22},std::vector<std::int16_t>(4),odh::VmCallReturnMode::ordinary,true};
    parked=queue.prepare_icon_owner_update(frame,instruction,dispatch,3,world.find(3)->generation,
        empty?std::optional<odh::VmCall>{}:std::optional<odh::VmCall>{child});
    require(parked.has_value(),"active20 prepares exact IconOwner update");
    require(!queue.action_tree_for(frame,instruction,{dispatch.origin,dispatch.ticket,true}),"check VM cannot borrow active ActionTree proof");
    if(stale_icon){require(world.remove(3)&&world.create(3,odh::EntityKind::object),"recycle icon before commit");
      require(!parked->can_commit(),"recycled icon generation invalidates plan");return {odh::PrimitiveDisposition::goto_false,{}};}
    if(stale_epoch){require(queue.enqueue(key(4102),{1,2,2,22}),"enqueue after icon preparation");
      require(!parked->can_commit(),"queue sequence mutation invalidates icon preparation");return {odh::PrimitiveDisposition::goto_false,{}};}
    if(empty){committed=parked->commit();return {odh::PrimitiveDisposition::goto_true,{}};}
    odh::PrimitiveOutcome outcome{odh::PrimitiveDisposition::call,{}};outcome.call=child;
    outcome.call_commit=[this]{if(repeat_seed)replay=*parked;committed=parked->commit();return committed;};return outcome;
   },odh::RoutineOwners{{22,actions(nested,external)}});
   require(queue.bind(*vm),"bind icon queue to actual VM");
  }
  void action(bool immediate=false){
   require(vm->start(key(4097),{1,1,1,0},{0})==odh::VmStatus::running&&
       queue.enqueue_ts1(key(4100),{1,2,2,22},{},1,odh::InteractionQueueMode::normal,
           immediate?4U:0U,false,false),"enqueue real protected icon action");
   if(immediate)require(queue.try_run_immediate(),"inject actual immediate protected action");
   else require(vm->run(1)==odh::VmStatus::budget_exhausted,"ordinary IdleForInput pushes protected action");
   require(vm->run(1)==odh::VmStatus::budget_exhausted,"action changes StackObject without changing callee");
   if(nested)require(vm->run(1)==odh::VmStatus::budget_exhausted,"ordinary subroutine inherits active ActionTree");
   if(external){require(vm->run(1)==odh::VmStatus::waiting,"prepare opaque external service wait");
       require(vm->call_pending(vm->pending()->ticket,{key(4101),{1,2,3,22},{}}),"push unproved external service child");}
  }
 };
 {Graph main;require(main.vm->start(key(4096),{1,1,3,0})==odh::VmStatus::running&&
     main.vm->run(1)==odh::VmStatus::returned_true&&main.tree==std::optional<bool>{false}&&!main.committed,
     "Main20 is known non-action without mutating queued icon or target");}
 for(bool immediate:{false,true})for(bool nested:{false,true}){
  Graph f(nested);f.action(immediate);require(f.vm->run(1)==odh::VmStatus::budget_exhausted&&f.tree==std::optional<bool>{true}&&f.committed,
      "ordinary and immediate ancestors authorize icon update before functional child execution");
  const auto state=f.queue.snapshot();const auto& action=state.actions.back();
  require(action.icon_owner&&action.icon_owner->id==3&&action.icon_owner->generation==f.world.find(3)->generation&&
      action.call.context.callee==2&&action.call.context.stack_object==2&&f.vm->temps()[6]==0,
      "IconOwner is separate from target/callee and child has not executed during commit");
  require(!f.parked->can_commit()&&!f.parked->commit(),"icon plan is single use and cannot cross dispatch boundaries");
  odh::EntityThreadControllersSnapshot encoded{};encoded.interactions=state;
  const auto bytes=odh::encode_entity_thread_controllers_snapshot(encoded);
  require(bytes[0]==12&&bytes[1]==0,"bound icon action selects controller codec12");
  const auto decoded=odh::decode_entity_thread_controllers_snapshot(bytes);
  require(decoded.interactions.actions.back().icon_owner->id==3&&
      odh::encode_entity_thread_controllers_snapshot(decoded)==bytes,"weak icon identity roundtrips exactly");
  auto absent=encoded;for(auto& row:absent.interactions.actions){row.icon_owner.reset();
      row.call.context.entity_references_bound=false;row.call.context.caller_generation=0;
      row.call.context.callee_generation=0;row.call.context.stack_object_generation=0;row.call.action_tree_override.reset();}
  absent.schedule_idle_start=0;const auto old=odh::encode_entity_thread_controllers_snapshot(absent);
  require(old[0]==7&&odh::encode_entity_thread_controllers_snapshot(odh::decode_entity_thread_controllers_snapshot(old))==old,
      "absent icon retains canonical v7 bytes and defaults to original callee");
  require(f.vm->run(8)==odh::VmStatus::yielded,"functional child and action return before icon entity retirement");
  const auto weak=f.queue.snapshot();
  require(!f.vm->references_entity(3)&&f.world.remove(3)&&!f.queue.references_target(3),
      "weak icon does not pin retired storage after execution references have ended");
  odh::World restored;require(restored.restore_snapshot(f.world.snapshot()),"restore World after icon retirement");
  odh::InteractionQueue queue(restored,1,8);odh::Vm vm(Graph::main(),restored.vm_memory(),{},queue.primitive(),
      {{22,Graph::actions(nested)}});
  require(vm.restore_snapshot(f.vm->snapshot())&&queue.bind(vm)&&queue.restore_snapshot(weak),
      "fresh queue preserves retired icon metadata without binding to a new World identity");
 }
 for(unsigned mode=0;mode<3;++mode){Graph f;f.empty=mode==0;f.stale_icon=mode==1;f.stale_epoch=mode==2;f.action();
  f.vm->run(1);require(mode==0?f.committed:!f.committed,"empty action commits synchronously; stale identity/queue plans reject atomically");}
 {Graph f;f.action();require(f.queue.enqueue_ts1(key(4100),{1,2,2,22},{},1,
      odh::InteractionQueueMode::idle,4,false,false)&&f.queue.try_run_immediate()&&
      f.queue.snapshot().suspended_active.size()==1,"immediate child protects an already active ordinary parent");
  require(f.vm->run(1)==odh::VmStatus::budget_exhausted&&f.vm->run(1)==odh::VmStatus::budget_exhausted&&f.committed,
      "deepest protected immediate interaction owns the functional icon update");
  const auto state=f.queue.snapshot();require(!state.actions[0].icon_owner&&state.actions[1].icon_owner&&
      state.actions[1].icon_owner->id==3,"suspended parent's IconOwner is not overwritten by its child's RunFunctionalTree");
  for(unsigned mode=0;mode<3;++mode){odh::EntityThreadControllersSnapshot bad{};bad.interactions=state;
    if(mode==0)bad.interactions.actions[1].icon_owner->id=0;
    if(mode==1)bad.interactions.actions[1].icon_owner->generation=0;
    if(mode==2)bad.interactions.actions[1].call.exact_arguments=true;
    bool rejected=false;try{odh::encode_entity_thread_controllers_snapshot(bad);}catch(const odh::FormatError&){rejected=true;}
    require(rejected,"codec rejects invalid weak icon identities and unserialized exact queued-call extents");}
 }
 {Graph f(false,true);f.action();require(f.vm->run(1)==odh::VmStatus::budget_exhausted&&f.tree==std::optional<bool>{true}&&f.committed,
     "actual external source child inherits its accepted action ActionTree flag");}
 {Graph f;f.repeat_seed=true;f.action();require(f.vm->run(1)==odh::VmStatus::budget_exhausted&&f.seeded&&
      f.queue.snapshot().actions.front().icon_owner->id==3,"prepare repeated equal-value icon update");
  bool observed=false;
  require(f.vm->run(8,[&](const odh::VmTrace& trace){if(trace.key.id==4102&&trace.pc==0){
      observed=true;require(f.replay&&!f.replay->can_commit()&&!f.replay->commit(),
          "copied unconsumed plan with unchanged icon cannot replay during child trace after kernel commit marker ends");
  }})==odh::VmStatus::yielded&&observed&&f.committed,
      "equal-value legitimate icon commit succeeds while descriptor replay lacks the exact call-commit scope");
 }
}
void user_permission_checks() {
    using Permission=odh::InteractionPermission;
    struct Case {int age,type,greet,gender;std::uint32_t flags;Permission expected;const char* name;};
    const Case cases[]{
        {27,0,0,0,0,Permission::allowed,"adult resident allowed"},
        {17,0,0,0,16,Permission::denied,"NoChild boundary"},
        {18,0,0,0,64,Permission::denied,"NoAdult boundary"},
        {17,0,0,0,64,Permission::allowed,"NoAdult permits child"},
        {17,0,0,0,32,Permission::allowed,"NoDemoChild ignored by full TS1"},
        {27,1,1,0,0,Permission::denied,"ungreeted visitor denied"},
        {27,1,2,0,0,Permission::allowed,"greeted visitor allowed"},
        {27,1,0,0,1,Permission::allowed,"AllowVisitors permits ungreeted visitor"},
        {27,0,0,16,512,Permission::allowed,"cat permitted"},
        {27,0,0,8,512,Permission::denied,"dog excluded by cats-only row"},
        {27,0,0,24,1536,Permission::allowed,"both pet bits require both permissions"},
        {27,0,0,24,512,Permission::denied,"combined pet bits still enforce dog permission"},
        {27,0,0,0,512,Permission::allowed,"pinned TS1 permits human on cat flag"},
        {27,0,0,8,1024|64,Permission::allowed,"NoAdult ignores adult pet"},
        {17,0,0,8,1024|16,Permission::denied,"NoChild still restricts young pet"},
        {27,0,0,0,128,Permission::denied,"Debug disabled"},
        {27,0,0,0,2,Permission::allowed,"execution flags do not become permissions"},
        {17,1,0,8,(1U<<28)|128,Permission::allowed,"SkipPermissions bypasses permission block"}
    };
    odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});world.create(2,odh::EntityKind::object);
    world.declare_object_word(1,8,16);
    for(const auto& item:cases){
        world.set_person_data(1,58,static_cast<std::int16_t>(item.age));
        world.set_person_data(1,32,static_cast<std::int16_t>(item.type));
        world.set_person_data(1,34,static_cast<std::int16_t>(item.greet));
        world.set_person_data(1,65,static_cast<std::int16_t>(item.gender));
        require(odh::ts1_interaction_permission(world.find(1),item.flags)==item.expected,item.name);
    }
    world.create(3,odh::EntityKind::avatar);
    require(odh::ts1_interaction_permission(world.find(3),0)==Permission::unsupported,
        "missing saved person data is unknown, not allowed or denied");
    world.set_person_data(1,58,27);world.set_person_data(1,32,0);world.set_person_data(1,65,0);
    const odh::RoutineSet main{{key(4096),bhav({idle()})}};
    const odh::Instruction object_available{2,254,255,{25,0,0,0,0,2,4,7}};
    const odh::RoutineOwners owners{{22,{{key(4097),bhav({set(1,33)})},
        {key(4098),bhav({set(0,71)})},{key(4099),bhav({object_available})}}}};
    odh::InteractionQueue queue(world,1);odh::Vm vm(main,world.vm_memory(),{},queue.primitive(),owners);
    queue.bind(vm);vm.start(key(4096),{1,1,1,0},{});
    require(queue.enqueue_user(key(4097),{1,2,2,22},{},16,key(4098)),"adult selects checked NoChild user action");
    const auto request_uid=queue.last_enqueued_uid();
    require(queue.contains_uid(request_uid),"UI tracks exact queued request identity");
    odh::EntityThreadControllersSnapshot state{};state.interactions=queue.snapshot();
    const auto decoded=odh::decode_entity_thread_controllers_snapshot(odh::encode_entity_thread_controllers_snapshot(state));
    require(decoded.interactions.actions.front().check_permissions&&
        decoded.interactions.actions.front().priority==50&&decoded.interactions.actions.front().flags==16,
        "user request saves original flags, permission provenance and priority50 without skip");
    odh::InteractionQueue restored(world,1);odh::Vm restored_vm(main,world.vm_memory(),{},restored.primitive(),owners);
    require(restored_vm.restore_snapshot(vm.snapshot())&&restored.bind(restored_vm)&&
        restored.restore_snapshot(decoded.interactions),"pending user request restores with permission recheck");
    world.set_person_data(1,58,17);
    require(vm.run(10)==odh::VmStatus::returned_true&&queue.size()==0&&!queue.contains_uid(request_uid)&&
        !queue.completion()&&vm.temps()[0]==0&&vm.temps()[1]==0&&
        restored_vm.run(10)==odh::VmStatus::returned_true&&restored.size()==0&&restored_vm.temps()==vm.temps(),
        "permission changing after click blocks check and action equally before and after save/reopen");
    world.set_person_data(1,58,27);world.declare_object_word(2,25,0);
    vm.start(key(4096),{1,1,1,0},{});
    require(queue.enqueue_user(key(4097),{1,2,2,22},{},0,key(4099)),
        "user selects object while its availability condition is true");
    world.declare_object_word(2,25,1);
    require(vm.run(10)==odh::VmStatus::returned_true&&queue.size()==0&&vm.temps()[1]==0,
        "object becoming unavailable while queued prevents action at dispatch");
    odh::InteractionQueue unknown_queue(world,3);
    odh::Vm unknown_vm(main,world.vm_memory(),{},unknown_queue.primitive(),owners);
    unknown_queue.bind(unknown_vm);unknown_vm.start(key(4096),{3,3,3,0},{});
    require(unknown_queue.enqueue_user(key(4097),{3,2,2,22},{},0)&&
        unknown_vm.run(10)==odh::VmStatus::fault&&unknown_queue.size()==1&&!unknown_queue.active(),
        "unknown permission state remains an explicit error with pending action intact");
}
void eligibility_checks() {
    odh::World world;world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);
    world.declare_object_word(1,8,16);world.declare_object_word(2,25,0);
    odh::InteractionQueue queue(world,1);
    // Failed check writes TEMP and source ObjectData; both effects survive.
    const odh::Instruction source_write{2,255,255,{25,0,9,0,0,5,4,7}};
    const odh::Instruction check_argument{2,254,255,{0,0,0,0,0,2,9,7}};
    const odh::RoutineSet main{{key(4096),bhav({idle(1,1),set(2,99)})}};
    const odh::RoutineOwners owners{{22,{{key(4097),bhav({set(1,33)})},
              {key(4098),bhav({set(0,71,1),source_write})},
              {key(4099),bhav({check_argument})},
              {key(4100),bhav({{15,254,255,{}}})}}}};
    odh::Vm vm(main,world.vm_memory(),{},queue.primitive(),owners);
    require(queue.bind(vm),"check fixture binds existing resident VM");
    vm.start(key(4096),{1,1,1,0},{0});
    require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false,key(4098))&&
            queue.enqueue_ts1(key(4097),{1,2,2,22},{},40,odh::InteractionQueueMode::normal,0,false,false,key(4099)),
            "checks are attached with exact action code owner");
    const auto saved_vm=vm.snapshot();
    odh::EntityThreadControllersSnapshot saved_controllers{};
    saved_controllers.interactions=queue.snapshot();
    const auto restored_state=odh::decode_entity_thread_controllers_snapshot(
        odh::encode_entity_thread_controllers_snapshot(saved_controllers)).interactions;
    odh::InteractionQueue restored_queue(world,1);
    odh::Vm restored_vm(main,world.vm_memory(),{},restored_queue.primitive(),owners);
    require(restored_vm.restore_snapshot(saved_vm)&&restored_queue.bind(restored_vm)&&
            restored_queue.restore_snapshot(restored_state),
            "pending check identity survives encoding and binds to exact restored owner");
    auto incomplete=owners;incomplete.at(22).erase(key(4098));
    odh::InteractionQueue missing_queue(world,1);
    odh::Vm missing_vm(main,world.vm_memory(),{},missing_queue.primitive(),incomplete);
    require(missing_vm.restore_snapshot(saved_vm)&&missing_queue.bind(missing_vm)&&
            !missing_queue.restore_snapshot(restored_state)&&missing_queue.size()==0,
            "restore refuses a check missing from its owner without publishing queue");
    require(vm.run(10)==odh::VmStatus::yielded&&queue.size()==1&&queue.active()&&
            vm.temps()[0]==71&&vm.temps()[1]==33&&world.find(2)->object_words.at(25)==9,
            "failed check keeps effects, drops only candidate, and following passing check starts action same tick");
    require(vm.stack().size()==1&&vm.stack().front().pc==0&&vm.stack().front().arguments[0]==-1,
            "checks preserve suspended Main and decrement its idle argument once");
    require(restored_vm.run(1)==odh::VmStatus::budget_exhausted&&restored_queue.active()&&
            restored_vm.run(10)==odh::VmStatus::yielded&&restored_vm.temps()==vm.temps()&&
            restored_queue.size()==queue.size(),
            "restored check outcome and action match even with a one-instruction Main slice");
    require(queue.observe()&&queue.tick(1)&&vm.run(10)==odh::VmStatus::returned_true,
            "checked action preserves next tick completion contract");
    vm.start(key(4096),{1,1,1,0},{0});
    require(!queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false,key(4999)),
            "missing exact check routine is rejected before enqueue");
    require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false,key(4100)),
            "unsupported check primitive fixture enqueued");
    require(vm.run(10)==odh::VmStatus::fault&&!queue.active()&&queue.size()==1&&vm.stack().size()==1&&
            vm.stack().front().arguments[0]==0&&vm.diagnostic().find("check")!=std::string::npos,
            "unsupported check never masquerades as false or successful action and leaves Main frame intact");
}
void dispatch_priority_checks() {
    for(const auto current:{std::int16_t{50},std::int16_t{2},std::int16_t{0}}){
        odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});
        world.create(2,odh::EntityKind::object);world.declare_object_word(1,8,128|16);
        world.set_person_data(1,33,current);
        odh::InteractionQueue queue(world,1);
        odh::Vm vm({{key(4096),bhav({idle()})}},world.vm_memory(),{},queue.primitive(),
            {{22,{{key(4097),bhav({set(1,33)})},{key(4098),bhav({set(0,71)})}}}});
        require(queue.bind(vm),"priority gate queue binds");vm.start(key(4096),{1,1,1,0},{});
        require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},current,odh::InteractionQueueMode::normal,
            0,false,false,key(4098)),"priority gate queues equal priority check");
        if(current==50){
            require(vm.run(10)==odh::VmStatus::returned_true&&!queue.active()&&queue.size()==1&&vm.temps()[0]==0,
                "equal high priority remains pending and does not execute check");
        }else{
            require(vm.run(1)==odh::VmStatus::budget_exhausted&&queue.active()&&vm.temps()[0]==71&&
                (world.find(1)->object_words.at(8)&128)==0&&(world.find(1)->object_words.at(8)&16)!=0,
                "equal low priority check starts and clears only cancellation flag");
            world.declare_object_word(1,8,128|16);
            require(vm.run(10)==odh::VmStatus::yielded&&queue.observe()&&
                world.find(1)->object_words.at(8)==16,"normal completion clears cancellation preserving other flags");
        }
    }
    {
        odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});
        world.create(2,odh::EntityKind::object);world.declare_object_word(1,8,16);
        odh::InteractionQueue queue(world,1);
        const odh::Instruction change_priority{2,255,255,{33,0,100,0,0,5,18,7}};
        odh::Vm vm({{key(4096),bhav({idle()})}},world.vm_memory(),{},queue.primitive(),
            {{22,{{key(4097),bhav({set(1,33)})},{key(4098),bhav({change_priority})}}}});
        queue.bind(vm);vm.start(key(4096),{1,1,1,0},{});
        require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,
            0,false,false,key(4098))&&queue.enqueue_ts1(key(4097),{1,2,2,22},{},40,
            odh::InteractionQueueMode::normal,0,false,false),"priority-mutating check fixture enqueued");
        require(vm.run(10)==odh::VmStatus::yielded&&queue.active()&&queue.size()==1&&
            world.find(1)->person_data[33]==40,
            "failed check priority mutation does not change this attempt's captured comparison threshold");
    }
    {
        odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});
        world.create(2,odh::EntityKind::object);world.declare_object_word(1,8,128|16);
        odh::InteractionQueue queue(world,1);
        odh::Vm vm({{key(4096),bhav({idle()})}},world.vm_memory(),{},queue.primitive(),
            {{22,{{key(4097),bhav({set(1,33)})},{key(4098),bhav({set(0,71)})}}}});
        queue.bind(vm);vm.start(key(4096),{1,1,1,0},{});
        require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},40,odh::InteractionQueueMode::parent_idle,
            0,false,false,key(4098)),"cancelled ParentIdle fixture enqueued");
        require(vm.run(10)==odh::VmStatus::returned_true&&queue.size()==0&&vm.temps()[0]==0&&
            world.find(1)->object_words.at(8)==(128|16),
            "cancelled ParentIdle is skipped without executing check or clearing cancellation");
    }
}
void contextual_push_owner_checks() {
    for(const bool same_vm:{true,false}){
        odh::World world;
        require(world.create(1,odh::EntityKind::avatar)&&world.create(3,odh::EntityKind::avatar)&&
            world.create(2,odh::EntityKind::object)&&world.declare_object_word(1,8,16)&&
            world.declare_object_word(3,8,16),"contextual Push has source object without any Main thread");
        odh::InteractionQueue caller_queue(world,1),other_queue(world,3);
        odh::Vm* caller=nullptr;odh::Vm* other=nullptr;unsigned lookups=0,loads=0;
        const auto program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{
            {key(4100),bhav({set(0,77)})},{key(4101),bhav({set(3,88)})}});
        const auto push=odh::ts1_push_interaction_primitive(world,
            odh::Ts1PushInteractionContextLookup{[&](const auto& request,const auto& frame,
                    const auto& instruction,const auto& dispatch)->std::optional<odh::Ts1PushInteractionAction>{
                ++lookups;
                require(request.source==2&&request.actor==(same_vm?1:3)&&dispatch.origin==caller&&
                    caller->matches_active_primitive(frame,instruction,dispatch),
                    "contextual lookup receives exact active Push dispatch and decoded source/recipient");
                auto& receiver=same_vm?*caller:*other;
                if(same_vm){
                    require(!receiver.install_shared_owner(44,program),
                        "ordinary public bank installation remains forbidden during executing recipient Main");
                    require(receiver.prepare_primitive_owner(frame,instruction,dispatch,44),
                        "same-VM Push authenticates bank admission without starting its queued action");
                }else require(receiver.install_shared_owner(44,program),
                    "cross-VM Push installs exact immutable bank at the recipient's idle safe point");
                return odh::Ts1PushInteractionAction{key(4100),44,0,key(4101)};
            }},[&](std::int16_t actor){return actor==1?odh::Ts1PushInteractionTarget{caller,&caller_queue}:
                actor==3?odh::Ts1PushInteractionTarget{other,&other_queue}:odh::Ts1PushInteractionTarget{};});
        // Argument0 is the source object ID; the authored idle countdown uses
        // separate Argument1 (zero), rather than waiting source-ID ticks.
        auto main_idle=idle(1,2);main_idle.operands[0]=1;
        const auto main=odh::RoutineSet{{key(4096),bhav({{13,1,255,{7,0,0,0,0,0,0,0}},
            main_idle,set(2,99)})}};
        odh::Vm caller_instance(main,world.vm_memory(),{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
            return i.opcode==13?push(f,i,t,d):caller_queue.primitive()(f,i,t,d);
        });
        odh::Vm other_instance({{key(4096),bhav({idle(1,1),set(2,99)})}},world.vm_memory(),{},other_queue.primitive());
        caller=&caller_instance;other=&other_instance;
        require(caller->set_owner_resolver([&](auto owner){++loads;
            require(owner==44,"same-VM owner resolver sees exact selected resource owner");return program;})&&
            caller_queue.bind(*caller)&&other_queue.bind(*other),"bind contextual Push recipient graphs");
        caller->start(key(4096),{1,1,static_cast<std::int16_t>(same_vm?1:3),0},{2});
        other->start(key(4096),{3,3,3,0});
        require(caller->run(1)==odh::VmStatus::budget_exhausted&&lookups==1&&loads==(same_vm?1U:0U),
            "Push publishes exactly once through contextual lookup before any action execution");
        auto& receiver=same_vm?*caller:*other;auto& queue=same_vm?caller_queue:other_queue;
        require(queue.size()==1&&receiver.temps()[0]==0&&receiver.temps()[3]==0&&
            queue.snapshot().actions.front().call.context.code_owner==44,
            "admitted owner stays attached to queued action without running action or check during lookup");
        require(receiver.run(20)==odh::VmStatus::yielded&&receiver.temps()[0]==77&&receiver.temps()[3]==88,
            "queued same/cross-VM action and check use the newly admitted owner in the recipient thread");
        require(queue.observe()&&queue.completion()&&queue.completion()->result,
            "contextual queued action publishes a successful completion");
        require(queue.tick(1),"contextual action resumes recipient Main on the next tick");
        const auto resumed=receiver.run(20);
        if(resumed!=odh::VmStatus::returned_true||receiver.temps()[2]!=99)
            throw std::runtime_error("contextual Main resume fixture same="+std::to_string(same_vm)+
                " status="+odh::vm_status_name(resumed)+" temp2="+std::to_string(receiver.temps()[2])+
                " vm="+receiver.diagnostic()+" queue="+queue.diagnostic());
        require(true,"contextual admission preserves normal Main completion");
    }
}
void native_push_interaction_checks() {
    odh::World world;
    // Inherited priority reads source PersonData33. Other banks stay minimal.
    world.create(1,odh::EntityKind::avatar,{0,34,0,0});world.create(3,odh::EntityKind::avatar);
    world.create(2,odh::EntityKind::object);world.declare_object_word(2,25,0);
    world.declare_object_word(1,8,16);world.declare_object_word(3,8,16);
    odh::InteractionQueue caller_queue(world,1),recipient_queue(world,3);
    odh::Vm* caller_vm=nullptr;odh::Vm* recipient_vm=nullptr;
    std::optional<odh::Ts1PushInteractionRequest> decoded;
    bool present=true,wrong_owner=false,throw_lookup=false;
    std::uint32_t action_flags=0;
    const auto push=odh::ts1_push_interaction_primitive(world,
        [&](const odh::Ts1PushInteractionRequest& request)->std::optional<odh::Ts1PushInteractionAction>{
            decoded=request;
            if(throw_lookup)throw odh::FormatError("fixture malformed resource");
            if(!present)return std::nullopt;
            return odh::Ts1PushInteractionAction{key(4097),wrong_owner?99U:22U,action_flags,key(4098)};
        },[&](std::int16_t actor){
            if(actor==1)return odh::Ts1PushInteractionTarget{caller_vm,&caller_queue};
            if(actor==3)return odh::Ts1PushInteractionTarget{recipient_vm,&recipient_queue};
            return odh::Ts1PushInteractionTarget{};
        });
    const odh::Instruction push_instruction{13,254,255,{7,0,0,0,0,0,0,0}};
    const odh::Instruction capture_caller{2,1,255,{0,0,11,0,0,5,8,3}};
    const odh::Instruction write_source{2,254,255,{25,0,7,0,0,5,4,7}};
    const auto caller_primitive=[&](const auto& f,const auto& i,const auto& t,const auto& d){
        return i.opcode==13?push(f,i,t,d):caller_queue.primitive()(f,i,t,d);
    };
    odh::Vm caller({{key(4096),bhav({push_instruction})}},world.vm_memory(),{},caller_primitive,
        {{22,{{key(4097),bhav({capture_caller,write_source})},{key(4098),bhav({set(3,88)})}}}});
    odh::Vm recipient({{key(4096),bhav({idle(1,1),set(2,99)})}},world.vm_memory(),{},
        recipient_queue.primitive(),{{22,{{key(4097),bhav({capture_caller,write_source})},{key(4098),bhav({set(3,88)})}}}});
    caller_vm=&caller;recipient_vm=&recipient;
    require(caller_queue.bind(caller)&&recipient_queue.bind(recipient),"PushInteraction residents bind independent queues");
    caller.start(key(4096),{1,1,3,0},{2});recipient.start(key(4096),{3,3,3,0},{0});
    require(caller.run(1)==odh::VmStatus::returned_true&&recipient_queue.size()==1&&caller_queue.size()==0,
            "native PushInteraction targets StackObject resident rather than invoking caller queue");
    require(decoded&&decoded->actor==3&&decoded->source==2&&decoded->interaction==7&&!decoded->custom_icon&&
            decoded->priority_code==0&&decoded->priority==1&&decoded->mode==odh::InteractionQueueMode::normal,
            "native PushInteraction decodes inherited priority and argument source");
    const auto queued=recipient_queue.snapshot();
    require(queued.actions.front().call.context.caller==3&&queued.actions.front().call.context.callee==2&&
            queued.actions.front().call.context.stack_object==2&&queued.actions.front().call.context.code_owner==22&&
            queued.actions.front().check&&queued.actions.front().check->id==4098&&
            queued.actions.front().interaction_number==7&&queued.actions.front().call.exact_arguments&&
            queued.actions.front().call.arguments==std::vector<std::int16_t>(4,0)&&
            queued.actions.front().argument_mode==odh::InteractionArgumentMode::source_null&&
            queued.actions.front().icon_owner&&queued.actions.front().icon_owner->id==2&&
            queued.actions.front().icon_owner->generation==world.find(2)->generation,
            "pushed action retains recipient Caller and exact source owner");
    require(recipient.run(10)==odh::VmStatus::yielded&&recipient.temps()[0]==3&&recipient.temps()[3]==88&&
            world.find(2)->object_words.at(25)==7,"recipient executes pushed action in its existing VM and shared World");
    require(recipient_queue.observe()&&recipient_queue.completion()->result&&recipient_queue.tick(1)&&
            recipient.run(10)==odh::VmStatus::returned_true&&recipient.temps()[2]==99,
            "pushed action completion resumes recipient Main on next tick");

    // Self-targeting uses the same resolver while that entity's Main is live.
    caller.start(key(4096),{1,1,1,0},{2});
    odh::VmFrame frame{key(4096),0,{1,1,1,0},{2},{2,0,77}};
    auto local_icon=push_instruction;local_icon.operands[3]=7;local_icon.operands[4]=2;
    require(push(frame,local_icon,caller.temps(),{&caller,1}).disposition==odh::PrimitiveDisposition::goto_true&&
            caller_queue.size()==1&&decoded&&decoded->actor==1&&decoded->source==2&&decoded->push_head&&
            decoded->custom_icon==77,"self-target local-source PushHead decodes custom icon without actor substitutions");
    auto tail=push_instruction;tail.operands[3]=128;
    require(push(frame,tail,caller.temps(),{&caller,2}).disposition==odh::PrimitiveDisposition::goto_true&&
            decoded->push_tail&&!decoded->push_head&&caller_queue.size()==2,
            "inherited PushTail continuation appends to recipient FIFO");
    const auto before=caller_queue.size();
    present=false;
    require(push(frame,push_instruction,caller.temps(),{&caller,3}).disposition==odh::PrimitiveDisposition::goto_false&&
            caller_queue.size()==before,"missing GetAction result selects false without queue mutation");
    present=true;wrong_owner=true;
    require(push(frame,push_instruction,caller.temps(),{&caller,4}).disposition==odh::PrimitiveDisposition::unsupported&&
            caller_queue.size()==before,"recipient missing exact owner fails explicitly before enqueuing");
    wrong_owner=false;throw_lookup=true;
    require(push(frame,push_instruction,caller.temps(),{&caller,5}).disposition==odh::PrimitiveDisposition::unsupported&&
            caller_queue.size()==before,"malformed action resource does not change recipient queue");
    throw_lookup=false;
    for(const auto operands:{std::array<std::uint8_t,8>{7,4,0,0,0,0,0,0},
                            {7,0,0,1,9,0,0,0}}){
        auto invalid=push_instruction;invalid.operands=operands;
        require(push(frame,invalid,caller.temps(),{&caller,6}).disposition==odh::PrimitiveDisposition::unsupported&&
                caller_queue.size()==before,"unsupported PushInteraction indices/priority/mode preserve queues");
    }
    const std::array<std::int16_t,7> expected_priorities{1,100,2,50,40,30,0};
    const std::array<odh::InteractionQueueMode,7> expected_modes{
        odh::InteractionQueueMode::normal,odh::InteractionQueueMode::normal,odh::InteractionQueueMode::normal,
        odh::InteractionQueueMode::normal,odh::InteractionQueueMode::parent_idle,
        odh::InteractionQueueMode::parent_exit,odh::InteractionQueueMode::idle};
    for(std::uint8_t code=1;code<=6;++code){
        // Decode each source priority on a new Main run. Accumulating these
        // unrelated cases would now invoke legitimate automatic suffix skips;
        // those effects and their EntryPoint4 hooks have their own matrix.
        caller.start(key(4096),{1,1,1,0},{2});
        auto prioritized=push_instruction;prioritized.operands[2]=code;
        require(push(frame,prioritized,caller.temps(),{&caller,static_cast<std::uint64_t>(20U+code)}).disposition==odh::PrimitiveDisposition::goto_true&&
            decoded&&decoded->priority_code==code&&decoded->priority==expected_priorities[code]&&
            decoded->mode==expected_modes[code]&&caller_queue.size()==1&&
            caller_queue.snapshot().actions.front().priority==expected_priorities[code]&&
            caller_queue.snapshot().actions.front().mode==expected_modes[code],
            "PushInteraction maps every TS1 priority to priority/mode");
    }
    // Exhaust every authored byte without allocating 65k queues: GetAction
    // known-missing is a source false return after the complete decode.
    present=false;frame.locals={3,0,77,2};frame.arguments={2};frame.action_tree=false;
    for(unsigned flags=0;flags<256;++flags)for(unsigned code=0;code<256;++code) {
        auto instruction=push_instruction;instruction.operands[2]=static_cast<std::uint8_t>(code);
        instruction.operands[3]=static_cast<std::uint8_t>(flags);
        const auto result=push(frame,instruction,caller.temps(),{&caller,1000});
        const auto expected_priority=code<7?expected_priorities[code]:std::int16_t{0};
        const auto expected_mode=code<7?expected_modes[code]:odh::InteractionQueueMode::normal;
        require(result.disposition==odh::PrimitiveDisposition::goto_false&&decoded&&
            decoded->priority_code==code&&decoded->priority==expected_priority&&decoded->mode==expected_mode&&
            decoded->source==((flags&2)?3:2)&&decoded->actor==1&&decoded->interaction==7&&
            decoded->push_head==((flags&4)!=0)&&decoded->push_tail==((flags&128)!=0)&&
            decoded->custom_icon==((flags&1)?std::optional<std::int16_t>{3}:std::nullopt),
            "all flag/priority bytes obey source masks and unknown-priority zero fallback");
    }
    require(world.set_person_data(1,33,60),"inherited source priority available");
    frame.action_tree=true;
    require(push(frame,push_instruction,caller.temps(),{&caller,1001}).disposition==odh::PrimitiveDisposition::goto_false&&decoded->priority==60,
        "inherited priority uses current frame ActionTree even without interaction return-mode ancestors");
    caller.start(key(4096),{1,1,1,0},{2});
    require(caller.inject_immediate(odh::VmCall{key(4097),{1,2,2,22},{},odh::VmCallReturnMode::ordinary}).has_value(),
        "real immediate ancestor available for non-action child override");
    frame.action_tree=false;
    require(push(frame,push_instruction,caller.temps(),{&caller,1002}).disposition==odh::PrimitiveDisposition::goto_false&&decoded->priority==1,
        "non-action frame override does not borrow immediate ancestor ActionTree");
    present=true;
    // The real SchoolBus Go to Work instruction: local3 source, local0 icon,
    // inherited priority, ignored bit8. No bus-specific branch is needed.
    for(const auto icon:{std::int16_t{2},std::int16_t{3},std::int16_t{77}}) {
        caller.start(key(4096),{1,1,1,0},{2});frame.locals={icon,0,0,2};
        const odh::Instruction schoolbus{13,254,255,{1,3,0,0x0b,0,0,0,0}};
        require(push(frame,schoolbus,caller.temps(),{&caller,1003}).disposition==odh::PrimitiveDisposition::goto_true,
            "real SchoolBus operands enqueue with ignored bit8 and custom icon");
        const auto state=caller_queue.snapshot();
        require(state.actions.size()==1&&state.actions.front().interaction_number==1&&
            state.actions.front().check&&state.actions.front().call.context.callee==2&&
            (icon!=77?(state.actions.front().icon_owner&&state.actions.front().icon_owner->id==icon&&
                state.actions.front().icon_owner->generation==world.find(icon)->generation):!state.actions.front().icon_owner),
            "custom icon retains live identity or source-null without changing action target/check");
        odh::EntityThreadControllersSnapshot controllers{};controllers.interactions=state;
        const auto bytes=odh::encode_entity_thread_controllers_snapshot(controllers);
        const auto reopened=odh::decode_entity_thread_controllers_snapshot(bytes);
        require(odh::encode_entity_thread_controllers_snapshot(reopened)==bytes&&
            reopened.interactions.actions.front().icon_owner.has_value()==(icon!=77),
            "pushed source icon and null override survive controller save/reopen exactly");
        odh::World reopened_world;require(reopened_world.restore_snapshot(world.snapshot()),"restore pushed-icon World");
        odh::InteractionQueue reopened_queue(reopened_world,1);
        odh::Vm reopened_vm({{key(4096),bhav({push_instruction})}},reopened_world.vm_memory(),{},reopened_queue.primitive(),
            {{22,{{key(4097),bhav({capture_caller,write_source})},{key(4098),bhav({set(3,88)})}}}});
        require(reopened_vm.restore_snapshot(caller.snapshot())&&reopened_queue.bind(reopened_vm)&&
            reopened_queue.restore_snapshot(reopened.interactions)&&
            reopened_queue.snapshot().actions.front().icon_owner.has_value()==(icon!=77),
            "reopened real VM/queue accepts persisted live and source-null pushed icons");
    }
    for(const auto priority:{std::uint8_t{3},std::uint8_t{5}})for(const auto flags:{std::uint8_t{0},std::uint8_t{4}}) {
        caller.start(key(4096),{1,1,1,0},{2});frame.locals={2,0,0,2};action_flags=flags;
        auto combined=push_instruction;combined.operands={1,3,priority,0x87,0,0,0,0};
        require(push(frame,combined,caller.temps(),{&caller,1004}).disposition==odh::PrimitiveDisposition::goto_true,
            "head+tail with normal/ParentExit and TTAB immediate retain source enqueue behavior");
        const auto state=caller_queue.snapshot();const auto& action=state.actions.front();
        require(state.actions.size()==1&&action.priority==expected_priorities[priority]&&
            action.mode==(flags?odh::InteractionQueueMode::idle:expected_modes[priority])&&
            (action.flags&0x70000000U)==0x70000000U&&action.check&&action.icon_owner,
            "TTAB RunImmediately changes mode without dropping continuation flags/check/icon");
    }
    action_flags=0;
    frame.context.stack_object=2;
    require(push(frame,push_instruction,caller.temps(),{&caller,7}).disposition==odh::PrimitiveDisposition::unsupported,
            "non-avatar target has explicit unsupported queue diagnostic");
    world.create(4,odh::EntityKind::avatar);frame.context.stack_object=4;
    require(push(frame,push_instruction,caller.temps(),{&caller,8}).disposition==odh::PrimitiveDisposition::unsupported,
            "unregistered resident target never falls back to another resident");

    odh::Interaction row_a{};row_a.action=4097;row_a.label_index=7;
    odh::Interaction row_b{};row_b.action=4098;row_b.label_index=2;
    odh::Ttab table{odh::TtabStatus::decoded,2,8,false,0,{row_a,row_b}};
    require(odh::ts1_interaction_by_index(table,2)==&table.interactions[1]&&
            odh::ts1_interaction_by_index(table,7)==&table.interactions[0]&&
            !odh::ts1_interaction_by_index(table,0),"TS1 GetAction resolves TTAIndex independently of row order");
    table.interactions.push_back(row_a);bool duplicate=false;
    try{(void)odh::ts1_interaction_by_index(table,7);}catch(const odh::FormatError&){duplicate=true;}
    require(duplicate,"ambiguous TTAIndex is malformed rather than silently choosing a row");
}
void immediate_queue_protocol() {
    constexpr std::uint32_t immediate=1U<<2;
    const odh::VmContext action_context{1,2,2,22};
    const auto audit_idle=[](const odh::VmPending& pending,const odh::VmFrame&){
        return pending.instruction.instruction.opcode==17;
    };
    { // Source TryRunImmediately scans protected entries too.
        Fixture f(1,4);
        require(f.queue.enqueue_ts1(key(4097),action_context,{},50,odh::InteractionQueueMode::normal,
            immediate,false,false)&&f.queue.has_immediate(),"immediate enqueue publishes one native identity");
        const auto main=f.vm.snapshot();
        require(f.queue.try_run_immediate()&&f.queue.active()&&f.vm.stack().size()==2&&
            f.vm.stack()[0].pc==main.stack[0].pc&&f.vm.steps()==main.steps&&
            f.vm.run_generation()==main.run_generation,
            "safe-point immediate injection leaves parent PC, work and run untouched");
        const auto ticket=f.queue.snapshot().active->ticket;
        require(f.queue.enqueue_ts1(key(4096),action_context,{},100,odh::InteractionQueueMode::normal,
            immediate,false,false),"second immediate appends after protected immediate");
        const auto sequence=f.vm.snapshot().ticket_sequence;
        require(f.queue.try_run_immediate()&&f.vm.stack().size()==2&&
            f.queue.snapshot().active->ticket==ticket&&f.vm.snapshot().ticket_sequence==sequence,
            "first protected immediate blocks later immediate without allocating a ticket");
        require(f.vm.run(20)==odh::VmStatus::yielded&&f.vm.immediate_return()&&!f.vm.interaction_return(),
            "immediate action publishes its separate unobserved return event");
        const auto returned=f.queue.snapshot();
        odh::InteractionQueue shadow(f.world,1,4);
        require(shadow.bind(f.vm)&&shadow.restore_snapshot(returned),
            "queue restore recognizes popped immediate frame with exact own return event");
        auto wrong_ticket=returned;++wrong_ticket.active->ticket;
        odh::InteractionQueue invalid(f.world,1,4);
        require(invalid.bind(f.vm)&&!invalid.restore_snapshot(wrong_ticket),
            "unobserved immediate return cannot authorize a different active ticket");
        require(f.queue.observe()&&f.queue.completion()&&
            !f.queue.completion()->result&&!f.queue.snapshot().resume_tick&&
            f.vm.status()==odh::VmStatus::running&&f.vm.stack()[0].pc==0&&
            f.vm.stack()[0].arguments[0]==0,
            "false immediate completes in same tick without branching or decrementing parent");
        require(f.queue.try_run_immediate()&&f.vm.run(20)==odh::VmStatus::yielded&&
            f.queue.observe()&&!f.queue.has_immediate()&&f.queue.size()==0&&
            f.vm.run(20)==odh::VmStatus::returned_true&&f.vm.temps()[2]==99,
            "next immediate may run after first ACK and parent resumes within same host tick");
    }
    { // The unusual source AllowPush path can reach a queued immediate normally.
        Fixture f;
        require(f.queue.enqueue_ts1(key(4097),action_context,{},50,odh::InteractionQueueMode::normal,
            immediate,false,false)&&f.vm.run(20)==odh::VmStatus::yielded&&f.vm.immediate_return()&&
            f.vm.immediate_continuations().back().parent_pending==std::nullopt&&
            f.vm.stack()[0].arguments[0]==-1&&f.queue.observe()&&!f.queue.snapshot().resume_tick&&
            f.vm.run(20)==odh::VmStatus::returned_true&&f.vm.temps()[2]==99,
            "AllowPush-selected immediate uses mode3 return and reexecutes its parent without false branch");
    }
    { // A parent Idle17 and a child Idle17 share one source clock, no parked timer.
        odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});world.create(2,odh::EntityKind::object);
        world.declare_object_word(1,8,0);
        odh::InteractionQueue queue(world,1);
        const odh::RoutineSet main{{key(4096),bhav({idle(0,1),set(2,99)})}};
        const odh::RoutineOwners owners{{22,{{key(4097),bhav({idle(0,1),set(1,33,255)})}}}};
        odh::Vm vm(main,world.vm_memory(),{},queue.primitive(),owners);
        require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0},{10})==odh::VmStatus::running,
            "Idle17 immediate fixture binds actor and parent");
        std::optional<std::uint64_t> shared=0;unsigned reads=0,writes=0;
        queue.set_schedule_idle_clock([&]{++reads;return shared;},[&](auto value){++writes;shared=value;});
        queue.set_immediate_parent_resume([&](auto ticket){return queue.retry_immediate_parent(ticket);});
        require(queue.tick(1)&&vm.run(20)==odh::VmStatus::waiting&&shared==1&&reads==1&&writes==2&&
            queue.snapshot().wait->deadline==11&&vm.stack()[0].arguments[0]==9,
            "Idle17 reads once, resets shared clock and schedules AllowPush0 with source start");
        const auto parent_ticket=vm.pending()->ticket;
        require(queue.enqueue_ts1(key(4097),action_context,{1},50,odh::InteractionQueueMode::normal,
            immediate,false,false)&&queue.tick(2,true)&&vm.pending()->ticket==parent_ticket,
            "deferred clock stage leaves parent idle pending before immediate injection");
        bool reentry_rejected=false;
        require(queue.try_run_immediate(audit_idle,[&](auto ticket){
            try{(void)queue.snapshot();}catch(const std::logic_error&){reentry_rejected=true;}
            return ticket==parent_ticket&&!queue.park_immediate_parent_wait(ticket+1)&&
                queue.park_immediate_parent_wait(ticket);
        })&&reentry_rejected&&!queue.waiting()&&shared==1&&
            vm.immediate_continuations().back().parent_pending->ticket==parent_ticket&&
            !vm.complete(parent_ticket,true),
            "audited park drops only exact old idle job, preserving parent pending and shared clock");
        require(vm.run(20)==odh::VmStatus::waiting&&queue.waiting()&&shared==2&&
            queue.snapshot().wait->deadline==3&&vm.stack()[0].arguments[0]==9,
            "child idle owns current queue wait while parent argument is parked unchanged");
        const auto saved_vm=vm.snapshot();const auto saved_queue=queue.snapshot();
        odh::EntityThreadControllersSnapshot encoded_state{};encoded_state.interactions=saved_queue;
        const auto decoded_queue=odh::decode_entity_thread_controllers_snapshot(
            odh::encode_entity_thread_controllers_snapshot(encoded_state)).interactions;
        require(decoded_queue.actions.front().call.return_mode==odh::VmCallReturnMode::immediate&&
            decoded_queue.wait->ticket==saved_queue.wait->ticket,
            "controller wire preserves active immediate mode and child idle ticket");
        odh::World restored_world;
        require(restored_world.restore_snapshot(world.snapshot()),"restore fresh World for immediate queue graph");
        odh::InteractionQueue restored(restored_world,1);
        odh::Vm restored_vm(main,restored_world.vm_memory(),{},restored.primitive(),owners);
        require(restored_vm.restore_snapshot(saved_vm)&&restored.bind(restored_vm)&&
            restored.restore_snapshot(decoded_queue),"active child idle plus parked parent restores exact queue and VM graph");
        auto bad=saved_queue;bad.actions[0].flags=0;
        odh::InteractionQueue wrong_mode(restored_world,1);require(wrong_mode.bind(restored_vm)&&!wrong_mode.restore_snapshot(bad),
            "snapshot rejects immediate mode without RunImmediately flag");
        std::optional<std::uint64_t> restored_shared=shared;
        restored.set_schedule_idle_clock([&]{return restored_shared;},[&](auto value){restored_shared=value;});
        restored.set_immediate_parent_resume([&](auto ticket){return restored.retry_immediate_parent(ticket);});
        for(unsigned restored_path=0;restored_path<2;++restored_path){
            auto& q=restored_path?restored:queue;auto& v=restored_path?restored_vm:vm;
            require(q.tick(3,true)&&v.status()==odh::VmStatus::waiting&&q.service_wait()&&
                v.run(20)==odh::VmStatus::yielded&&v.immediate_return()&&q.observe()&&
                !q.snapshot().resume_tick&&!q.waiting()&&v.status()==odh::VmStatus::running&&
                v.stack()[0].pc==0&&v.stack()[0].arguments[0]==9,
                "immediate completion ACK retries exact parent Idle17 in same tick without restoring its old timer");
            require(v.run(20)==odh::VmStatus::waiting&&v.stack()[0].arguments[0]==8&&
                q.snapshot().wait->deadline==12,
                "parent uses child's current zero clock, not its obsolete start1/deadline11");
        }
        require(shared==3&&restored_shared==shared&&vm.temps()==restored_vm.temps(),
            "reopened immediate idle has identical shared clock and registers");
    }
    { // Preempt an ordinary action's audited Sleep with async Create42.
        odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});world.create(2,odh::EntityKind::object);
        world.declare_object_word(1,8,0);odh::InteractionQueue queue(world,1);
        const odh::RoutineSet main{{key(4096),bhav({idle(1,1),set(2,99)})}};
        const odh::RoutineOwners owners{{22,{{key(4097),bhav({{0,1,255,{}},set(0,22)})},
            {key(4098),bhav({{42,1,255,{}},set(1,33,255)})}}}};
        const auto primitive=[&](const auto& f,const auto& i,const auto& t,const auto& d)->odh::PrimitiveOutcome{
            if(i.opcode==0||i.opcode==42)return {odh::PrimitiveDisposition::wait,{}};
            return queue.primitive()(f,i,t,d);
        };
        odh::Vm vm(main,world.vm_memory(),{},primitive,owners);
        require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0},{3})==odh::VmStatus::running&&
            queue.enqueue_ts1(key(4097),action_context,{},10,odh::InteractionQueueMode::normal,0,false,false)&&
            vm.run(20)==odh::VmStatus::waiting,"ordinary action sleeps before immediate preemption");
        const auto old=vm.pending()->ticket;const auto old_pc=vm.stack()[1].pc;
        require(queue.enqueue_ts1(key(4098),action_context,{},50,odh::InteractionQueueMode::normal,
            immediate,false,false),"immediate async child appends without cancelling active action");
        const auto before=vm.snapshot();bool parked=false;
        const auto deny=[](const auto&,const auto&){return false;};
        require(!queue.try_run_immediate(deny,[&](auto){parked=true;return true;})&&!parked&&
            vm.pending()->ticket==old&&vm.snapshot().ticket_sequence==before.ticket_sequence,
            "unaudited sleeping parent refuses injection without VM mutation or park callback");
        queue.set_immediate_parent_resume([&](auto ticket){return parked&&ticket==old&&vm.retry_pending(ticket);});
        require(queue.try_run_immediate([](const auto& pending,const auto&){return pending.instruction.instruction.opcode==0;},
            [&](auto ticket){parked=ticket==old;return parked;})&&queue.snapshot().suspended_active.size()==1&&
            vm.run(20)==odh::VmStatus::waiting&&vm.pending()->instruction.instruction.opcode==42&&
            !vm.complete(old,true),"audited Sleep is parked independently while immediate Create42 waits");
        const auto child=vm.pending()->ticket;
        require(vm.complete(child,true)&&vm.run(20)==odh::VmStatus::yielded&&queue.observe()&&
            queue.active()&&queue.snapshot().suspended_active.empty()&&!queue.snapshot().resume_tick&&
            vm.status()==odh::VmStatus::running&&vm.stack()[1].pc==old_pc&&
            queue.completion()->result==false,
            "false Create child completion restores ordinary action and retries Sleep without changing parent PC");
    }
    { // CheckAction shares thread registers and source writes even on false.
        odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});world.create(2,odh::EntityKind::object);
        world.declare_object_word(1,8,0);world.declare_object_word(2,25,0);
        odh::InteractionQueue queue(world,1);
        const odh::Instruction source_write{2,255,255,{25,0,9,0,0,5,4,7}};
        odh::Vm vm({{key(4096),bhav({idle()})}},world.vm_memory(),{},queue.primitive(),
            {{22,{{key(4097),bhav({set(1,33)})},{key(4098),bhav({set(0,71,1),source_write})}}}});
        require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0},{0})==odh::VmStatus::running&&
            queue.enqueue_user(key(4097),action_context,{},immediate,key(4098)),
            "checked immediate user request captures permission and BHAV provenance");
        const auto before=vm.snapshot();
        require(queue.try_run_immediate()&&!queue.active()&&queue.size()==0&&vm.temps()[0]==71&&
            world.find(2)->object_words.at(25)==9&&vm.steps()==before.steps&&
            vm.snapshot().ticket_sequence==before.ticket_sequence&&vm.stack()[0].pc==before.stack[0].pc,
            "false immediate check drops request but retains shared TEMP/World effects without parent work");
    }
    {
        odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});world.create(2,odh::EntityKind::object);
        world.declare_object_word(1,8,0);world.set_person_data(1,58,17);
        odh::InteractionQueue queue(world,1);
        odh::Vm vm({{key(4096),bhav({idle()})}},world.vm_memory(),{},queue.primitive(),
            {{22,{{key(4097),bhav({set(1,33)})},{key(4098),bhav({set(0,71)})}}}});
        require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0},{0})==odh::VmStatus::running&&
            queue.enqueue_user(key(4097),action_context,{},immediate|16,key(4098))&&
            queue.try_run_immediate()&&queue.size()==0&&!queue.active()&&vm.temps()[0]==0&&vm.temps()[1]==0,
            "immediate permission denial drops request before Check or action effects");
    }
    {
        Fixture f(1,1);
        require(f.queue.enqueue_ts1(key(4096),action_context,{},50,odh::InteractionQueueMode::normal,immediate,false,false),
            "bounded queue accepts one immediate");
        const auto uid=f.queue.last_enqueued_uid();
        require(!f.queue.enqueue_ts1(key(4097),action_context,{},50,odh::InteractionQueueMode::normal,immediate,false,false)&&
            f.queue.last_enqueued_uid()==uid,"queue cap rejects before consuming immediate native UID");
        f.world.remove(2);f.world.create(2,odh::EntityKind::object);
        const auto ticket=f.vm.snapshot().ticket_sequence;
        require(f.queue.try_run_immediate()&&!f.queue.active()&&f.queue.size()==0&&
            f.vm.snapshot().ticket_sequence==ticket,"stale queued immediate target is dropped without running against replacement");
    }
    {
        odh::World world;world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);
        odh::InteractionQueue queue(world,1);odh::VmLimits limits;limits.stack_depth=1;
        odh::Vm vm({{key(4096),bhav({idle()})}},world.vm_memory(),limits,queue.primitive(),
            {{22,{{key(4097),bhav({set(0,71)})}}}});
        require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0},{0})==odh::VmStatus::running&&
            queue.enqueue_ts1(key(4097),action_context,{},50,odh::InteractionQueueMode::normal,immediate,false,false),
            "VM frame-cap fixture queues immediate request");
        const auto before=vm.snapshot();
        require(!queue.try_run_immediate()&&queue.size()==1&&!queue.active()&&
            vm.stack().size()==1&&vm.snapshot().ticket_sequence==before.ticket_sequence&&vm.steps()==before.steps,
            "VM frame cap rejects atomically before publishing immediate frame or ticket");
    }
}

}
namespace {
struct SourceArgsFixture {
    odh::World world;odh::InteractionQueue queue;std::vector<std::int16_t> observed;
    bool observed_exact=false;std::vector<std::int16_t> check_arguments;bool check_exact=false;odh::Vm vm;
    SourceArgsFixture(bool start=true):queue(world,1,8),
      vm({{key(4096),bhav({idle()})}},world.vm_memory(),{},
        [this](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
            if(instruction.opcode==90){observed=frame.arguments;observed_exact=frame.exact_arguments;
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};}
            return queue.primitive()(frame,instruction,temps,dispatch);
        },{{22,{{key(4097),odh::Bhav{0x8002,{{90,254,255,{}}},{},odh::Bhav::Metadata{0,9,0,0}}},
                 {key(4098),odh::Bhav{0x8002,{{91,254,255,{}}},{},odh::Bhav::Metadata{0,9,0,0}}}}}}) {
        world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);
        world.declare_object_word(1,8,16);require(queue.bind(vm),"bind exact source-Args fixture queue");
        queue.set_check_primitive([this](const auto& frame,const auto&,const auto&,const auto&){
            check_arguments=frame.arguments;check_exact=frame.exact_arguments;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};});
        if(start)vm.start(key(4096),{1,1,1,0},{0});
    }
};
void source_queue_arguments_tests() {
    const std::vector<std::optional<std::vector<std::int16_t>>> cases={std::nullopt,std::vector<std::int16_t>{},
        std::vector<std::int16_t>{7,-8},std::vector<std::int16_t>{1,2,3,4,5,6,7,8,9,10}};
    for(const auto& source_args:cases)for(const bool immediate:{false,true}){
        SourceArgsFixture f;
        const auto flags=static_cast<std::uint32_t>((1U<<28)|(immediate?4U:0U));
        require(f.queue.enqueue_source_user(key(4097),{1,2,2,22},source_args,flags,key(4098),3),
            "source user action accepts explicit nullable Args without routine-count inference");
        const auto expected=source_args?*source_args:std::vector<std::int16_t>(4,0);
        const auto before=f.queue.snapshot();
        require(before.actions.front().call.exact_arguments&&before.actions.front().call.arguments==expected&&
            before.actions.front().argument_mode==(source_args?odh::InteractionArgumentMode::source_explicit:odh::InteractionArgumentMode::source_null),
            "queued source action distinguishes null Args from explicit arrays and preserves exact values");
        if(immediate)require(f.queue.try_run_immediate(),"source immediate action injects its exact argument frame");
        else require(f.vm.run(1)==odh::VmStatus::budget_exhausted,"source normal action enters its exact child frame");
        require(f.vm.stack().back().exact_arguments&&f.vm.stack().back().arguments==expected&&
            f.vm.stack().back().return_mode==(immediate?odh::VmCallReturnMode::immediate:odh::VmCallReturnMode::interaction_next_tick),
            "action metadata requiring nine args does not pad source null/empty/explicit frame extents");
        require(f.check_exact&&f.check_arguments==std::vector<std::int16_t>(4,0),
            "source check BHAV uses independent exact four zeros despite nine declared parameters and different action Args");
        odh::EntityThreadControllersSnapshot controls{};controls.interactions=f.queue.snapshot();
        const auto encoded=odh::encode_entity_thread_controllers_snapshot(controls);
        require(encoded.front()==12,"exact source action Args and bound queued references select controller codec12");
        const auto restored_controls=odh::decode_entity_thread_controllers_snapshot(encoded);
        SourceArgsFixture restored(false);
        require(restored.vm.restore_snapshot(f.vm.snapshot())&&restored.queue.restore_snapshot(restored_controls.interactions),
            "fresh equivalent VM and queue restore exact normal/immediate action extents");
        require(restored.vm.run(1)==odh::VmStatus::yielded&&restored.observed==expected&&restored.observed_exact,
            "restored source action executes with exact Args and its original return mode");
        auto invalid=restored_controls.interactions;invalid.actions.front().call.exact_arguments=false;
        SourceArgsFixture wrong(false);require(wrong.vm.restore_snapshot(f.vm.snapshot())&&!wrong.queue.restore_snapshot(invalid),
            "source argument provenance cannot restore through a padded call flag");
    }
    SourceArgsFixture legacy;
    require(legacy.queue.enqueue_user(key(4097),{1,2,2,22},{7,-8},1U<<28,key(4098))&&
        legacy.vm.run(1)==odh::VmStatus::budget_exhausted&&legacy.vm.stack().back().arguments.size()==9&&
        !legacy.vm.stack().back().exact_arguments&&!legacy.check_exact&&legacy.check_arguments.size()==9,
        "legacy/direct user enqueue and checks retain existing metadata padding");
    SourceArgsFixture pushed;
    require(pushed.queue.enqueue_source_ts1(key(4097),{1,2,2,22},std::nullopt,50,
        odh::InteractionQueueMode::normal,0,false,false,{},3)&&pushed.vm.run(1)==odh::VmStatus::budget_exhausted&&
        pushed.vm.stack().back().arguments==std::vector<std::int16_t>(4,0)&&pushed.vm.stack().back().exact_arguments,
        "source PushInteraction null Args uses exactly four zeros despite larger action metadata");
}
void interaction_number_provenance_tests() {
    for(const std::int32_t number:{std::int32_t{0},std::int32_t{3},std::int32_t{255},std::int32_t{65535},
                                  std::int32_t{-2147483647-1},std::int32_t{2147483647},std::int32_t{-1}}){
        Fixture source;
        require(source.queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,
            0,false,false,{},number),"source queue accepts exact signed32 TTAB identity independent of BHAV number");
        const auto state=source.queue.snapshot();
        require(state.actions.front().interaction_number==number&&state.actions.front().call.key.id==4097,
            "source queue preserves exact TTAB identity without using the routine id");
        Fixture restored;
        require(restored.queue.restore_snapshot(state)&&restored.queue.snapshot().actions.front().interaction_number==number,
            "fresh queue restore retains exact interaction provenance");
        Fixture selected;
        require(selected.queue.enqueue_user(key(4096),{1,2,2,22},{},0,{},number)&&
            selected.queue.snapshot().actions.front().interaction_number==number,
            "user-selected queue preserves original signed32 TTAB identity");
    }
    Fixture unknown;
    require(unknown.enqueue()&&!unknown.queue.snapshot().actions.front().interaction_number,
        "direct host queue work retains unknown TTAB identity");
    Fixture legacy;
    require(legacy.queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false)&&
        !legacy.queue.snapshot().actions.front().interaction_number,"older TS1 callers retain unknown TTAB identity");
}
}
void autonomous_check_regressions(){
 for(const auto variants:{false,true})for(const auto answer:{false,true}){
  odh::World world;require(world.create(1,odh::EntityKind::avatar,{1,88,0,16})&&world.create(2,odh::EntityKind::object)&&
   world.create(3,odh::EntityKind::object)&&world.create(4,odh::EntityKind::object),"autonomous check real entities");
  world.declare_object_word(1,50,7);world.set_person_data(1,58,25);
  const auto assign=[](unsigned scope,unsigned index,unsigned value,unsigned next){return odh::Instruction{2,static_cast<std::uint8_t>(next),255,
   {static_cast<std::uint8_t>(index),0,static_cast<std::uint8_t>(value),0,0,5,static_cast<std::uint8_t>(scope),7}};};
  auto tree=bhav({{99,1,255,{}},assign(8,5,91,2),assign(29,0,5,3),assign(12,0,100,4),assign(28,0,3,5),
   assign(3,50,123,6),assign(10,0,3,7),{50,8,255,{44,0,0,0,1,0,0,0}},assign(10,0,4,9),
   {50,static_cast<std::uint8_t>(answer?254:255),255,{44,0,0,0,2,0,0,0}}});
  if(!variants)tree.instructions[5].on_true=answer?254:255;
  auto global=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
  auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4097),bhav({set(0,0)})},{key(4098),tree}});
  odh::Vm* caller=nullptr;odh::AutonomyResult<odh::AutonomyCheck> result;unsigned observed=0;odh::VmMemory scoped;
  const auto checks=[&](const odh::VmFrame& frame,const auto&,const auto& temps,const auto& token){
   ++observed;require(token.check_mode&&frame.arguments==std::vector<std::int16_t>({1,0,0,0})&&frame.exact_arguments&&
    frame.context.caller==1&&frame.context.callee==2&&frame.context.stack_object==2&&frame.context.code_owner==22&&
    temps[5]==18&&world.find(1)->object_words.at(50)==0&&scoped.read(frame,12,0)==0&&
    scoped.read(frame,28,0)==0&&scoped.read(frame,29,0)==0,"AutoCheck actual context/Param0=1/Temps/HideInteraction and no active-ad fallback");
   return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};};
  odh::Vm vm({{key(4096),bhav({{3,254,255,{}}})}},world.vm_memory(),{},[&](const auto&,const auto&,const auto&,const auto&){
   odh::Interaction row{};row.action=4097;row.test=4098;row.motives.push_back({55,777,9});
   result=odh::evaluate_saved_autonomy_check(world,*caller,1,2,22,row,global,bank,world.vm_memory(),checks,
    [&](const auto& frame,const auto&,const auto&){
     require(scoped.read(frame,12,0)==100&&scoped.read(frame,28,0)==3&&scoped.read(frame,29,0)==5,
      "primitive operand memory and expressions share exact TreeAd scratch");
     return odh::AutonomyResult<std::optional<std::string>>{odh::AutonomyStatus::ready,"source-name",{}};
    },10000,[&](const odh::VmMemory& memory){scoped=memory;return checks;});
   return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};});
  caller=&vm;vm.start(key(4096),{1,1,1,0});vm.temps()[5]=18;
  require(vm.run(1)==odh::VmStatus::returned_true&&observed==1&&result.status==odh::AutonomyStatus::ready&&
   result.value.allowed==answer&&vm.temps()[5]==91&&world.find(1)->object_words.at(50)==123,
   "false AutoCheck retains actual effects and never applies menu visibility filter");
  if(variants)require(result.value.variants.size()==2&&result.value.variants[0].param0==3&&result.value.variants[1].param0==4&&
   !result.value.variants[0].motive_ad_changes&&!result.value.variants[1].motive_ad_changes,
   "action-string variants capture real StackObject and do not inherit TreeAds");
  else require(result.value.variants.size()==1&&result.value.variants[0].motive_ad_changes&&
   result.value.variants[0].motive_ad_changes->at(0)==5&&result.value.variants[0].motive_ad_changes->at(1U<<16)==100&&
   result.value.variants[0].motive_ad_changes->at(2U<<16)==3,"fallback variant carries three scratch advertisement banks");
 }
}
void generic_ts1_icon_tests(){
 struct Graph {
  odh::World world;odh::InteractionQueue queue{world,1,8};std::unique_ptr<odh::Vm> vm;
  bool nested=false,null_after=false;std::optional<odh::VmFrame> dispatched_frame;std::optional<odh::Instruction> dispatched_instruction;std::optional<odh::VmDispatch> dispatched;
  static odh::RoutineSet main(){return {{key(4096),bhav({idle(1)})}};}
  odh::RoutineOwners owners()const{
   const odh::Instruction icon{1,2,255,{2,91,0,0,0,0,0,0}}; // Only byte0 is mode; other bytes are not invented flags.
   return {{22,{{key(4100),bhav({{2,1,255,{0,0,3,0,0,5,10,7}},
       nested?odh::Instruction{4101,2,255,{}}:icon,null_after?odh::Instruction{4102,254,255,{}}:set(6,99)})},
      {key(4101),bhav({{1,254,255,{2,0,0,0,0,0,0,0}}})},
      {key(4102),bhav({{2,1,255,{0,0,0,0,0,5,10,7}},{1,254,255,{2,0,0,0,0,0,0,0}}})}}}};
  }
  explicit Graph(bool ordinary_child=false,bool clear_after=false):nested(ordinary_child),null_after(clear_after){
   require(world.create(1,odh::EntityKind::avatar,{0,88,0,16})&&world.create(2,odh::EntityKind::object)&&world.create(3,odh::EntityKind::object)&&
       world.declare_object_word(1,8,0),"generic icon source entities");
   vm=std::make_unique<odh::Vm>(main(),world.vm_memory(),odh::VmLimits{},
    [this](const auto& f,const auto& i,const auto& t,const auto& d){
     if(i.opcode!=1)return queue.primitive()(f,i,t,d);
     require(i.operands[0]==2,"generic icon exact source mode");
     const auto before=queue.size();
     const auto denied=queue.set_action_icon_to_stack_object(f,i,{d.origin,d.ticket,true});
     require(denied.disposition==odh::PrimitiveDisposition::unsupported,"synchronous Check cannot borrow active icon authority");
     auto forged=f;forged.context.caller=2;
     require(queue.set_action_icon_to_stack_object(forged,i,d).disposition==odh::PrimitiveDisposition::unsupported,"wrong Caller cannot rewrite another queue icon");
     require(!queue.action_tree_for(f,i,d),"mode2 keeps separate actual opcode1 proof, never borrows opcode20 API");
     require(queue.size()==before,"rejected generic icon requests do not change queue authority");
     dispatched_frame=f;dispatched_instruction=i;dispatched=d;
     return queue.set_action_icon_to_stack_object(f,i,d);
    },owners());
   require(queue.bind(*vm)&&vm->start(key(4096),{1,1,1,0},{0})==odh::VmStatus::running,"generic icon actual persistent VM");
  }
  void start(bool immediate){
   require(queue.enqueue_ts1(key(4100),{1,2,2,22},{19},1,odh::InteractionQueueMode::normal,immediate?4U:0U,false,false),"generic icon actual queued action");
   if(immediate)require(queue.try_run_immediate(),"generic icon actual immediate ancestor");else require(vm->run(1)==odh::VmStatus::budget_exhausted,"generic icon normal action enters");
   require(vm->run(1)==odh::VmStatus::budget_exhausted,"generic icon source changes StackObject independently of target");
   if(nested)require(vm->run(1)==odh::VmStatus::budget_exhausted,"generic icon ordinary helper inherits actual ActionTree");
   require(vm->run(1)==odh::VmStatus::budget_exhausted,"generic icon assignment executes without running next action instruction");
  }
 };
 for(bool immediate:{false,true})for(bool nested:{false,true}){
  Graph f(nested);f.start(immediate);const auto state=f.queue.snapshot();const auto& action=state.actions.front();
  require(action.icon_owner&&action.icon_owner->id==3&&action.icon_owner->generation==f.world.find(3)->generation&&
   action.call.context.callee==2&&action.call.context.stack_object==2&&action.call.arguments==std::vector<std::int16_t>{19}&&f.vm->temps()[6]==0,
   "GenericTS1Call2 changes only actual source icon, not queue target or authored args");
  require(f.queue.set_action_icon_to_stack_object(*f.dispatched_frame,*f.dispatched_instruction,*f.dispatched).disposition==odh::PrimitiveDisposition::unsupported,
   "generic icon cannot replay completed dispatch authority");
  odh::EntityThreadControllersSnapshot stored{};stored.interactions=state;
  const auto encoded=odh::encode_entity_thread_controllers_snapshot(stored);const auto decoded=odh::decode_entity_thread_controllers_snapshot(encoded);
  require(encoded[0]==12&&decoded.interactions.actions.front().icon_owner->id==3&&
   decoded.interactions.actions.front().call.context.entity_references_bound&&
   decoded.interactions.actions.front().call.action_tree_override==std::optional<bool>{true}&&
   odh::encode_entity_thread_controllers_snapshot(decoded)==encoded,
   "GenericTS1Call2 preserves existing iconowner payload alongside exact queued references in codec12");
  odh::InteractionQueue resumed_queue(f.world,1,8);
  odh::Vm resumed(Graph::main(),f.world.vm_memory(),{},resumed_queue.primitive(),f.owners());
  require(resumed.restore_snapshot(f.vm->snapshot())&&resumed_queue.bind(resumed)&&resumed_queue.restore_snapshot(decoded.interactions)&&
   resumed_queue.snapshot().actions.front().icon_owner->generation==f.world.find(3)->generation,
   "generic icon owner survives fresh VM/queue restore");
  require(resumed.run(1)==odh::VmStatus::yielded&&resumed.temps()[6]==99&&resumed_queue.observe()&&resumed_queue.completion()&&resumed_queue.completion()->result,
   "restored action completes after real generic icon mutation");
 }
 {Graph f(false,true);f.start(false);require(f.queue.snapshot().actions.front().icon_owner.has_value(),"generic null-clear starts with real icon");
  // Authored ordinary child call, then actual source StackObjectID=0, mode2.
  require(f.vm->run(1)==odh::VmStatus::budget_exhausted&&f.vm->run(1)==odh::VmStatus::budget_exhausted&&
   f.vm->run(1)==odh::VmStatus::yielded&&!f.queue.snapshot().actions.front().icon_owner&&f.queue.observe()&&f.queue.completion()->result,
   "GenericTS1Call2 source-null assignment clears previous icon rather than succeeding without change");
 }
 {odh::World world;world.create(1,odh::EntityKind::avatar,{0,88,0,16});world.declare_object_word(1,8,0);odh::InteractionQueue queue(world,1,8);
  odh::Vm vm({{key(4096),bhav({{1,254,255,{2,0,0,0,0,0,0,0}}})}},world.vm_memory(),{},
   [&](const auto& f,const auto& i,const auto&,const auto& d){return queue.set_action_icon_to_stack_object(f,i,d);});
  require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::fault&&queue.size()==0,
   "Main without an actual active action cannot fake generic icon success");
 }
}
void gosub_found_action_regressions(){
 odh::World world;world.create(1,odh::EntityKind::avatar,{0,88,0,16});world.create(2,odh::EntityKind::object);
 world.declare_object_word(1,8,0);world.set_person_data(1,58,25);
 const auto main=[](){return odh::RoutineSet{{key(4096),bhav({{30,254,255,{}}})}};};
 const auto owners=[](){return odh::RoutineOwners{{22,{{key(4097),bhav({{2,1,255,{0,0,77,0,0,5,10,7}},
  {30,2,2,{}},set(5,73)})},{key(4098),bhav({{99,254,255,{}}})}}}};};
 odh::InteractionQueue queue(world,1,8);odh::Vm vm(main(),world.vm_memory(),{},queue.primitive(),owners());
 vm.start(key(4096),{1,1,77,0});queue.bind(vm);
 queue.set_check_primitive([&](const auto& frame,const auto&,const auto&,const auto& token){
  require(token.check_mode&&frame.context.stack_object==2,"handover check uses real queued target");
  return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};});
 require(queue.enqueue_source_autonomous(key(4097),{1,2,2,22},{19,0,0,0},0,key(4098),7),"queue source autonomous action");
 const auto queued=queue.snapshot();require(queued.actions[0].priority==2&&queued.actions[0].flags==0&&queued.actions[0].check_permissions&&
  queued.actions[0].call.arguments==std::vector<std::int16_t>({19,0,0,0}),"priority2 retains original flags/permissions/four arguments");
 std::optional<odh::InteractionActionSnapshot> higher;
 require(queue.first_higher_priority(1,higher)&&higher&&queue.first_higher_priority(2,higher)&&!higher,"shortcut priority comparison is strict");
 require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.stack().size()==2&&vm.stack()[0].pc==0&&queue.active(),
  "30 handover leaves original parent instruction unchanged, never synthetic17 or true branch");
 std::vector<odh::InteractionActionSnapshot> users;require(queue.active_actions(users)&&users.size()==1,"only protected active queue prefix is a user");
 const auto vm_state=vm.snapshot();const auto queue_state=queue.snapshot();
 odh::InteractionQueue restored(world,1,8);odh::Vm fresh(main(),world.vm_memory(),{},restored.primitive(),owners());
 require(fresh.restore_snapshot(vm_state)&&restored.bind(fresh)&&restored.restore_snapshot(queue_state),"fresh restore of actual30 handover");
 require(fresh.run(1)==odh::VmStatus::budget_exhausted&&fresh.stack().back().context.stack_object==77,
  "ordinary child changes source StackObject before30");
 require(fresh.run(1)==odh::VmStatus::budget_exhausted&&fresh.stack().back().context.stack_object==2&&fresh.stack().back().pc==2,
  "ActionTree30 first publishes ActiveAction.Callee then returns false");
 require(fresh.run(1)==odh::VmStatus::yielded&&restored.observe()&&restored.tick(1)&&fresh.run(1)==odh::VmStatus::returned_false,
  "completion resumes original30 and no-action path returns false");
}
void source_error_eligibility_matrix(){
 for(unsigned path=0;path<3;++path)for(unsigned mode=0;mode<5;++mode){
  odh::World world;world.create(1,odh::EntityKind::avatar,{0,88,0,16});world.create(2,odh::EntityKind::object);
  world.declare_object_word(1,8,0);world.set_person_data(1,58,25);
  odh::InteractionQueue queue(world,1,8);std::uint64_t checked_uid=0;unsigned checked=0;bool action_ran=false;
  const odh::VmPrimitive check_primitive=[&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
   ++checked;if(mode==4)throw std::runtime_error("authored absent object");
   return {mode==0?odh::PrimitiveDisposition::source_error:mode==1?odh::PrimitiveDisposition::unsupported:
     mode==2?odh::PrimitiveDisposition::wait:odh::PrimitiveDisposition::continue_execution,"authored absent object"};
  };
  queue.set_check_primitive(check_primitive);
  const auto enqueue_checked=[&](){
   const bool result=queue.enqueue_source_ts1(key(4097),{1,2,2,22},{},2,odh::InteractionQueueMode::normal,
      path==2?4U:0U,false,false,key(4098),7);
   checked_uid=queue.last_enqueued_uid();return result;
  };
  const odh::VmPrimitive primitive=[&](const auto& frame,const auto& ins,const auto& temps,const auto& dispatch)->odh::PrimitiveOutcome{
   if(ins.opcode==99){require(enqueue_checked(),"nested ERROR candidate enqueues by source path");return {odh::PrimitiveDisposition::goto_true,{}};}
   if(ins.opcode==97){action_ran=true;return {odh::PrimitiveDisposition::goto_true,{}};}
   return queue.primitive()(frame,ins,temps,dispatch);
  };
  odh::Vm vm({{key(4096),bhav({{30,254,255,{}}})}},world.vm_memory(),{},primitive,
   {{22,{{key(4097),bhav({{97,254,255,{}}})},{key(4098),bhav({{98,254,255,{}}})},
         {key(4099),bhav({{99,1,253,{}},idle(1)})}}}});
  require(queue.bind(vm)&&vm.start(key(4096),{1,1,1})==odh::VmStatus::running,"ERROR queue matrix has live Main");
  if(path==1)require(queue.enqueue_source_ts1(key(4099),{1,2,2,22},{},1,odh::InteractionQueueMode::normal,0,false,false),"source parent action enqueued");
  else require(enqueue_checked(),"ERROR candidate enqueued by normal source request");
  bool completed=false;
  if(path==2)completed=queue.try_run_immediate();
  else completed=vm.run(16)!=odh::VmStatus::fault;
  require(checked>0&&!action_ran,"every source ERROR matrix path actually checks and never runs rejected action");
  if(mode==0)require(completed&&!queue.contains_uid(checked_uid),"known source ERROR excludes ordinary/nested/immediate candidate without host failure");
  else require(!completed&&queue.contains_uid(checked_uid),"unsupported, async wait, budget and exceptions remain unresolved; no false eligibility decision");
 }
}
void autonomous_eligibility_context_regressions(){
 unsigned advertisement_checks=0;
 const auto factory=[&](const odh::VmMemory& effective){
  return odh::VmPrimitive{[&,effective](const auto& frame,const auto& instruction,const auto&,const auto&){
   if(instruction.opcode!=98)return odh::PrimitiveOutcome{};
   for(const auto scope:{std::uint8_t{12},std::uint8_t{28},std::uint8_t{29}}){
    require(effective.read(frame,scope,7)==std::optional<std::int16_t>{0},"queued checks start with no active-action advertisement fallback");
    require(effective.write(frame,scope,7,19)&&effective.read(frame,scope,7)==std::optional<std::int16_t>{19},"all queued-check operand scopes share the actual scratch advertisement map");
   }
   ++advertisement_checks;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
  }};
 };
 const auto programs=[](){return odh::RoutineOwners{{22,{
  {key(4097),bhav({{2,254,255,{1,0,0,0,0,5,8,9}}})}, // Temp1 = authored Param0.
  {key(4098),bhav({{98,1,255,{}},{2,254,255,{0,0,1,0,0,2,9,7}}})}, // Auto-only eligibility.
  {key(4099),bhav({{2,254,255,{0,0,1,0,0,2,7,7}}})}, // Known denial remains denial.
  {key(4100),bhav({{99,1,253,{}},{17,254,255,{0,0,1,0,0,0,0,0}}})}
 }}};};
 for(unsigned scenario=0;scenario<5;++scenario){
  odh::World world;require(world.create(1,odh::EntityKind::avatar,{0,88,0,16})&&world.create(2,odh::EntityKind::object)&&
   world.declare_object_word(1,8,0)&&world.set_person_data(1,58,25),"eligibility source avatar facts");
  odh::InteractionQueue queue(world,1,8);odh::Vm vm({{key(4096),bhav({{30,254,255,{}}})}},world.vm_memory(),{},queue.primitive(),programs());
  queue.set_check_primitive_factory(factory);
  require(vm.set_advertisement_fallback([](const auto&,auto,auto){return std::optional<std::int16_t>{73};}),"real Thread advertisement fallback installed");
  require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0})==odh::VmStatus::running,"eligibility source Main");
  const bool automatic=scenario==0||scenario==3||scenario==4,immediate=scenario==3;
  const auto check=key(scenario==4?4099:4098);const std::vector<std::int16_t> args{19,0,0,0};
  bool accepted=false;
  if(automatic)accepted=queue.enqueue_source_autonomous(key(4097),{1,2,2,22},args,immediate?4U:0U,check,7);
  else if(scenario==1)accepted=queue.enqueue_source_user(key(4097),{1,2,2,22},args,0,check,7);
  else accepted=queue.enqueue_source_ts1(key(4097),{1,2,2,22},args,2,odh::InteractionQueueMode::normal,0,false,false,check,7);
  require(accepted,"source producer accepts explicit eligibility context");
  const auto before=queue.snapshot();require(before.actions.front().call.arguments==args&&
   before.actions.front().check_context==(automatic?odh::InteractionCheckContext::autonomous:odh::InteractionCheckContext::default_zero),
   "only actual autonomous producer marks checks, authored variant argument19 retained");
  if(scenario==0){
   auto snapshot=vm.snapshot();odh::InteractionQueue restored(world,1,8);odh::Vm resumed({{key(4096),bhav({{30,254,255,{}}})}},world.vm_memory(),{},restored.primitive(),programs());
   restored.set_check_primitive_factory(factory);
   require(resumed.set_advertisement_fallback([](const auto&,auto,auto){return std::optional<std::int16_t>{73};}),"fresh real Thread fallback stays separate from scratch rechecks");
   require(resumed.restore_snapshot(snapshot)&&restored.bind(resumed)&&restored.restore_snapshot(before),"autonomous eligibility context survives fresh queue restore");
   require(resumed.run(1)==odh::VmStatus::budget_exhausted&&restored.active()&&
    resumed.stack().back().arguments==args,"auto-only eligibility check passes without changing executed action arguments");
   require(!resumed.advertisement_changes(),"reopened queued check leaves the real Thread dictionary null");
   require(resumed.run(1)==odh::VmStatus::yielded&&resumed.temps()[1]==19&&restored.observe()&&restored.completion()&&restored.completion()->result,
    "restored autonomous action actually completes with authored variant19");
  }else if(immediate){
   require(queue.try_run_immediate()&&queue.active()&&vm.run(1)==odh::VmStatus::yielded&&vm.temps()[1]==19&&queue.observe()&&queue.completion()->result,
    "immediate eligibility consumer preserves autonomous context and still executes authored args");
  }else{
   require(vm.run(8)==odh::VmStatus::returned_false&&!queue.active()&&queue.size()==0&&vm.temps()[1]==0,
    "manual/same-priority push or actual denied autonomous check cannot execute action");
  }
  require(!vm.advertisement_changes(),"ordinary and immediate eligibility discard scratch advertisements");
 }
 // A queued autonomous child can be checked from an already active action.
 odh::World world;world.create(1,odh::EntityKind::avatar,{0,88,0,16});world.create(2,odh::EntityKind::object);
 world.declare_object_word(1,8,0);world.set_person_data(1,58,25);odh::InteractionQueue queue(world,1,8);
 const auto primitive=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
  if(instruction.opcode!=99)return queue.primitive()(frame,instruction,temps,dispatch);
  require(queue.enqueue_source_autonomous(key(4097),{1,2,2,22},{19,0,0,0},0,key(4098),7),"nested actual autonomous producer");
  return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
 };
 odh::Vm vm({{key(4096),bhav({{30,254,255,{}}})}},world.vm_memory(),{},primitive,programs());
 queue.set_check_primitive_factory(factory);
 require(vm.set_advertisement_fallback([](const auto&,auto,auto){return std::optional<std::int16_t>{73};}),"nested action fallback exists only on real Thread");
 require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0})==odh::VmStatus::running&&
  queue.enqueue_source_ts1(key(4100),{1,2,2,22},std::vector<std::int16_t>{0,0,0,0},1,odh::InteractionQueueMode::normal,1024,false,false),"protected lower-priority source parent");
 require(vm.run(1)==odh::VmStatus::budget_exhausted&&queue.active()&&vm.run(1)==odh::VmStatus::budget_exhausted&&queue.size()==2,
  "nested autonomous action joins actual active parent");
 require(vm.run(1)==odh::VmStatus::budget_exhausted&&vm.stack().back().key.id==4097&&vm.stack().back().arguments[0]==19,
  "nested IdleForInput eligibility consumer uses explicit automatic context");
 require(vm.run(1)==odh::VmStatus::yielded&&vm.temps()[1]==19&&queue.observe()&&queue.completion()&&queue.completion()->result,
  "nested automatic action completes with original action arguments");
 require(!vm.advertisement_changes()&&advertisement_checks==5,"normal, restored, immediate and nested queue paths each isolate their scratch advertisements");
}
void active_advertisement_regressions(){
 odh::Ttab table{};table.status=odh::TtabStatus::decoded;
 odh::Interaction first{};first.label_index=0;first.motives={{-2,3,4}};
 odh::Interaction selected{};selected.label_index=257;selected.motives={{-17,29,65535},{0,0,0}};
 table.interactions={first,selected};
 require(odh::interaction_advertisement(&table,257,0,0)==-17&&
  odh::interaction_advertisement(&table,257,1,0)==29&&odh::interaction_advertisement(&table,257,2,0)==-1,
  "source advertisement types use exact TTAIndex instead of row ordinal or truncated byte");
 require(odh::interaction_advertisement(&table,static_cast<std::int32_t>(-2147483391LL),1,0)==0&&
  odh::interaction_advertisement(&table,257,1,2)==0&&odh::interaction_advertisement(nullptr,257,1,0)==0,
  "global high bit is not stripped and known missing table/row/motive returns zero");
 auto malformed=table;malformed.interactions.push_back(selected);
 require(!odh::interaction_advertisement(&malformed,257,1,0),"duplicate dictionary keys remain unavailable");
 Fixture f;const auto generation=f.world.find(2)->generation;unsigned reads=0;
 const odh::InteractionQueue::AdvertisementLookup lookup=[&](auto callee,auto number,auto type,auto index){
  require(callee.id==2&&callee.generation==generation,"active fallback retains the exact Callee generation");
  ++reads;return odh::interaction_advertisement(&table,number,type,index);
 };
 require(f.queue.enqueue_ts1(key(4096),{1,2,2,22},{},2,odh::InteractionQueueMode::normal,0,false,false,{},257),
  "pending action adopts the actual Main generation without activating its queue block");
 require(f.queue.active_advertisement(f.vm,1,0,lookup)==0&&reads==0,"no actual ActiveQueueBlock has no fallback even with pending queue entries");
 require(f.vm.run(1)==odh::VmStatus::budget_exhausted&&f.queue.active(),"actual interaction root admits a known TTAIndex");
 require(f.queue.active_advertisement(f.vm,1,0,lookup)==29&&reads==1,"active advertisement resolves the actual queued action metadata");
 const auto missing=odh::named_routine_key(400,false),overridden=odh::named_routine_key(401,false);
 require(f.vm.install_owner(33,{{missing,bhav({{2,254,255,{0,0,7,0,0,5,8,12}}})},
  {overridden,bhav({{2,1,255,{7,0,3,0,0,5,12,7}},{2,254,255,{7,0,3,0,0,2,12,7}}})}})&&
  f.vm.set_advertisement_fallback([&](const auto&,auto type,auto index){return f.queue.active_advertisement(f.vm,type,index,lookup);}),
  "actual active Thread binds explicit named resource and its real advertisement fallback");
 auto named_context= f.vm.stack().back().context;named_context.code_owner=33;
 const auto old_queue=f.queue.snapshot();
 const auto missing_result=f.vm.run_named_in_stack(missing,named_context,{},16);
 require(missing_result.started&&missing_result.status==odh::VmStatus::fault&&f.queue.snapshot().active->uid==old_queue.active->uid,
  "named empty queue preserves old ActiveQueueBlock fault instead of borrowing active TTAB ads");
 const auto override_result=f.vm.run_named_in_stack(overridden,named_context,{},16);
 require(override_result.started&&override_result.status==odh::VmStatus::returned_true&&f.vm.advertisement_changes()&&
  f.vm.advertisement_changes()->at((1U<<16)|7)==3&&f.queue.snapshot().active->uid==old_queue.active->uid,
  "named real Thread overrides still precede the empty-queue fallback and survive after restoration");
 require(f.world.remove_retaining_body({2,generation})&&f.world.create(2,odh::EntityKind::object)&&
  f.queue.active_advertisement(f.vm,1,0,lookup)==29&&reads==2,"same numeric ID reuse cannot change an old action's advertisement resource");
 odh::Vm foreign({{key(4096),bhav({set(0,0)})}},f.world.vm_memory());
 require(!f.queue.active_advertisement(foreign,1,0,lookup),"another Thread cannot borrow the real queue's active advertisement");
}
void named_empty_queue_idle_regressions(){
 for(unsigned scenario=0;scenario<4;++scenario){
  Fixture f;const auto named=odh::named_routine_key(402,false);
  require(f.queue.tick(1)&&f.queue.tick(2)&&f.queue.tick(3)&&f.queue.tick(4)&&f.queue.tick(5),"named empty-queue fixture owns the actual Thread clock");
  if(scenario==3)require(f.queue.enqueue_ts1(key(4096),{1,2,2,22},{},2,odh::InteractionQueueMode::normal,0,false,false)&&
   f.vm.run(1)==odh::VmStatus::budget_exhausted&&f.queue.active(),"named action-tree fixture has a real protected parent");
  require(f.vm.install_owner(33,{{named,bhav({idle(1)})}}),"named Idle resource installed without numeric scope remapping");
  std::uint64_t idle_start=3;unsigned resets=0,interrupt_reads=0;bool interrupt=scenario==1;
  f.queue.set_schedule_idle_clock([&]{return std::optional<std::uint64_t>{idle_start};},[&](auto value){idle_start=value;++resets;});
  f.queue.set_thread_interrupt_source([&]{++interrupt_reads;const auto value=interrupt;interrupt=false;return value;});
  auto context=f.vm.stack().back().context;context.code_owner=33;
  std::optional<std::int16_t> decremented;
  const auto handler=[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
   auto result=f.queue.primitive()(frame,instruction,temps,dispatch);if(result.write)decremented=result.write->value;return result;
  };
  const auto result=f.vm.run_named_in_stack(named,context,{scenario==0?std::int16_t{0}:std::int16_t{5}},16,handler);
  if(scenario<2)require(result.started&&result.status==odh::VmStatus::returned_true&&decremented==std::optional<std::int16_t>{scenario==0?std::int16_t{-2}:std::int16_t{3}}&&
   idle_start==0&&resets==1&&interrupt_reads==1,"synchronous named Idle decrements by real elapsed clock and handles interrupt/natural completion with no queue borrow");
  else require(result.started&&result.status==odh::VmStatus::fault&&idle_start==0&&resets==1&&
   interrupt_reads==(scenario==3?0U:1U)&&!f.queue.waiting(),"named unresolved scheduler/invalid ActiveAction faults retain the preceding real clock effect without inventing a wait");
 }
}
void autonomous_check_failure_regressions(){
 {
  odh::Ttab table{};table.status=odh::TtabStatus::decoded;
  odh::Interaction row{};row.label_index=1;table.interactions.push_back(row);
  row.label_index=257;table.interactions.push_back(row);odh::validate_saved_autonomy_table(table);
  row.label_index=1;table.interactions.push_back(row);bool duplicate=false;
  try{odh::validate_saved_autonomy_table(table);}catch(const odh::FormatError&){duplicate=true;}
  require(duplicate,"TTAB dictionary rejects exact duplicate indices, never aliases distinct byte-truncated IDs");
  odh::VmProgram global(odh::RoutineSet{});
  odh::VmProgram resource(odh::RoutineSet{{key(4097),bhav({set(0,0)})},{key(4098),bhav({set(0,0)})}});
  odh::Interaction selected{};selected.label_index=257;selected.action=4097;selected.test=4098;
  odh::Interaction truncated=selected;truncated.label_index=1;truncated.action=4998;
  require(static_cast<std::uint8_t>(selected.label_index)==truncated.label_index&&
   odh::saved_autonomy_action_available(selected,global,resource)&&!odh::saved_autonomy_action_available(truncated,global,resource),
   "byte-truncated final GetAction may have a known missing action before owner admission");
  truncated.action=4097;truncated.test=4999;
  require(!odh::saved_autonomy_action_available(truncated,global,resource),"byte-truncated final GetAction rejects known missing nonzero check");
 }
 for(unsigned mode=0;mode<3;++mode){
  odh::World world;world.create(1,odh::EntityKind::avatar,{1,88,0,16});world.create(2,odh::EntityKind::object);
  world.declare_object_word(1,50,44);world.set_person_data(1,58,25);
  auto global=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
  auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4097),bhav({set(0,0)})},
   {key(4098),bhav({set(5,92,1),{2,2,255,{0,0,7,0,0,5,0,7}},{99,2,255,{}}})}});
  odh::Vm* caller=nullptr;odh::AutonomyResult<odh::AutonomyCheck> result;
  const auto checks=[&](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{
   mode==0?odh::PrimitiveDisposition::goto_true:mode==1?odh::PrimitiveDisposition::wait:odh::PrimitiveDisposition::unsupported,"test nonreturn"};};
  odh::Vm vm({{key(4096),bhav({{3,254,255,{}}})}},world.vm_memory(),{},[&](const auto&,const auto&,const auto&,const auto&){
   odh::Interaction row{};row.action=4097;row.test=4098;
   result=odh::evaluate_saved_autonomy_check(world,*caller,1,2,22,row,global,bank,world.vm_memory(),checks,{},3);
   return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
  });caller=&vm;vm.start(key(4096),{1,1,1,0});
  require(vm.run(1)==odh::VmStatus::returned_true&&result.status==odh::AutonomyStatus::unsupported&&
   vm.temps()[5]==92&&world.find(1)->attributes[0]==7,"AutoCheck budget/wait/fault retain actual effects without fabricating no-action");
  odh::Interaction row{};row.action=4097;row.test=4999;
  world.declare_object_word(1,50,123);
  const auto missing=odh::evaluate_saved_autonomy_check(world,vm,1,2,22,row,global,bank,world.vm_memory(),checks,{});
  require(missing.status==odh::AutonomyStatus::ready&&!missing.value.allowed&&world.find(1)->object_words.at(50)==0,
   "known missing nonzero CheckRoutine gives null GetAction and still resets HideInteraction");
  row.test=0;row.flags=128;world.declare_object_word(1,50,123);
  const auto denied=odh::evaluate_saved_autonomy_check(world,vm,1,2,22,row,global,bank,world.vm_memory(),checks,{});
  require(denied.status==odh::AutonomyStatus::ready&&!denied.value.allowed&&world.find(1)->object_words.at(50)==0,
   "denied permission still clears real HideInteraction before CheckAction");
 }
}
void autonomous_primitive_memory_regressions(){
 for(const auto scope:{12U,28U,29U,26U}){
  odh::World world;world.create(1,odh::EntityKind::avatar,{1,88,0,16});world.create(2,odh::EntityKind::object);
  world.set_person_data(1,58,25);
  const auto write=[&](unsigned selected,unsigned value,unsigned next){return odh::Instruction{2,static_cast<std::uint8_t>(next),255,
   {0,0,static_cast<std::uint8_t>(value),0,0,5,static_cast<std::uint8_t>(selected),7}};};
  auto global=std::make_shared<const odh::VmProgram>(odh::RoutineSet{});
  auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key(4097),bhav({set(0,0)})},
   {key(4098),bhav({write(12,100,1),write(28,3,2),write(29,5,3),
    {25,254,255,{3,static_cast<std::uint8_t>(scope),0,0,0,0,0,0}}})}});
  odh::Vm* caller=nullptr;std::int32_t family_budget=1000;
  odh::AutonomyResult<odh::AutonomyCheck> result;
  odh::Vm vm({{key(4096),bhav({{3,254,255,{}}})}},world.vm_memory(),{},[&](const auto&,const auto&,const auto&,const auto&){
   auto memory=world.vm_memory();const auto read=memory.read;
   memory.read=[read](const auto& frame,std::uint8_t selected,std::int16_t index)->std::optional<std::int16_t>{
    if(selected==26)return 7;
    return read(frame,selected,index);};
   odh::Interaction row{};row.action=4097;row.test=4098;
   result=odh::evaluate_saved_autonomy_check(world,*caller,1,2,22,row,global,bank,std::move(memory),{}, {},100,
    [&](const odh::VmMemory& effective){return odh::ts1_budget_primitive(effective,
     [&](const auto&)->std::optional<std::int32_t>{return family_budget;},
     [&](const auto&,std::int32_t value){family_budget=value;return true;});});
   return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
  });caller=&vm;vm.start(key(4096),{1,1,1,0});
  const auto amount=scope==12?100:scope==28?3:scope==29?5:7;
  require(vm.run(1)==odh::VmStatus::returned_true&&result.status==odh::AutonomyStatus::ready&&result.value.allowed&&
   family_budget==1000-amount,"real Budget primitive retaining VmMemory reference sees scratch TreeAds and exact local tuning");
 }
}
void generation_reference_tests(){
 {Fixture f;
  require(f.vm.start(key(4096),{1,1,2,0},{5})==odh::VmStatus::running&&f.queue.observe(),"Main carries real StackObject reference and empty queue adopts its run");
  const auto original=f.world.find(2)->generation;
  require(f.world.remove_retaining_body({2,original})&&f.world.create(2,odh::EntityKind::object),"delete body releases numeric ID immediately");
  require(f.world.find(2)->generation!=original&&f.world.frame_entity(f.vm.stack().back(),odh::VmEntityRole::stack_object)->generation==original,
      "Main sees old body while numeric lookup sees replacement");
  require(f.queue.source_reset_required()==std::optional<bool>{false},"foreign dead StackObject in Main does not Reset");
  require(f.enqueue(),"new action targets live replacement");
  const auto action=f.queue.snapshot().actions.front();
  require(action.call.context.callee_generation==f.world.find(2)->generation&&action.call.action_tree_override==std::optional<bool>{true},
      "accepted queue binds new generation and source ActionTree producer");
 }
 {Fixture f;require(f.enqueue()&&f.vm.run(1)==odh::VmStatus::budget_exhausted,"queued root entered by actual IdleForInput");
  require(f.vm.stack().back().action_tree,"accepted action root owns source ActionTree flag");
  auto state=f.vm.snapshot();auto queue=f.queue.snapshot();state.action_tree_known=false;
  for(auto& frame:state.stack)frame.action_tree=false;
  require(odh::InteractionQueue::upgrade_legacy_action_tree(state,queue)&&state.action_tree_known&&state.stack.back().action_tree&&!state.stack.front().action_tree,
      "legacy action upgrade proves root and preserves Main outside tree");
  auto bad=f.vm.snapshot();bad.action_tree_known=false;auto wrong=f.queue.snapshot();++wrong.active->ticket;
  require(!odh::InteractionQueue::upgrade_legacy_action_tree(bad,wrong)&&!bad.action_tree_known,"wrong legacy ticket cannot manufacture ActionTree");
  const auto original=f.world.find(2)->generation;
  require(f.world.remove_retaining_body({2,original})&&f.world.create(2,odh::EntityKind::object),"active callee body retained during ID reuse");
  require(f.queue.source_reset_required()==std::optional<bool>{true},"top ActionTree and actual dead Queue0 callee require source Reset");
 }
}
int main(int argc,char** argv){try {
 source_error_eligibility_matrix();
 generation_reference_tests();active_advertisement_regressions();named_empty_queue_idle_regressions();
 autonomous_check_regressions();autonomous_check_failure_regressions();autonomous_primitive_memory_regressions();generic_ts1_icon_tests();gosub_found_action_regressions();autonomous_eligibility_context_regressions();
 if(argc==4 && std::string(argv[1])=="--native-global") {
  std::ifstream input(argv[2],std::ios::binary);if(!input)throw std::runtime_error("native archive absent");
  odh::View archive(input,std::filesystem::file_size(argv[2]));std::optional<odh::Bhav> wrapper;bool matched=false;
  for(const auto& entry:odh::far1_index(archive))if(entry.name==argv[3]) {
   if(matched)throw std::runtime_error("ambiguous native entry");matched=true;
   odh::iff_chunks(archive.subview(entry.offset,entry.length),[&](const auto& chunk){
    if(chunk.type=="BHAV"&&chunk.id==281){if(wrapper)throw std::runtime_error("ambiguous native BHAV281");wrapper=odh::read_bhav(chunk.payload);}
   });
  }
  require(wrapper.has_value(),"native global wrapper selected explicitly");
  odh::World world;world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);world.declare_object_word(1,8,16);
  odh::InteractionQueue queue(world,1);
  odh::Instruction invoke{281,1,1,{0,0,1,0,0,0,0,0}};
  odh::Vm vm({{key(4096),bhav({invoke,set(2,99)})},{key(281),*wrapper}},world.vm_memory(),{},queue.primitive(),
      {{22,{{key(4096),bhav({set(0,22)})}}}});
  require(queue.bind(vm),"native wrapper controller bound");vm.start(key(4096),{1,1,1,0});
  require(queue.enqueue(key(4096),{1,2,2,22})&&vm.run(30)==odh::VmStatus::yielded,"native281 AllowPush hands over to explicit owner22 action");
  require(vm.stack().size()==2&&vm.stack().back().key.id==281&&vm.stack().back().pc==4,"native wrapper same IdleForInput pc survives action return");
  require(queue.observe()&&queue.size()==0&&queue.tick(1)&&vm.run(30)==odh::VmStatus::returned_true&&vm.temps()[0]==22&&vm.temps()[2]==99,
          "native wrapper resumes next tick then returns through its own flag test");
  require(world.find(1)->object_words.at(8)==16,"native push path preserves unrelated flags");
  std::cout<<checks<<" native-wrapper ODH checks passed; action/Main fixtures synthetic\n";return 0;
 }
 if(argc!=1)throw std::runtime_error("usage: --native-global ARCHIVE ENTRY");
 source_queue_arguments_tests();
 interaction_number_provenance_tests();
 immediate_queue_protocol();
 functional_icon_tests();user_permission_checks();
 eligibility_checks();
 dispatch_priority_checks();
 contextual_push_owner_checks();
 native_push_interaction_checks();
 {odh::World world;world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);
  world.create(3,odh::EntityKind::avatar);world.declare_object_word(2,25,0);
  odh::InteractionQueue mover_queue(world,1),blocker_queue(world,3);
  const auto main=bhav({idle(),set(2,99)});
  const odh::Instruction write_destination{2,254,255,{25,0,7,0,0,5,4,7}};
  const auto shoo_action=bhav({write_destination});
  odh::Vm mover({{key(4096),main}},world.vm_memory(),{},mover_queue.primitive());
  odh::Vm blocker({{key(4096),main}},world.vm_memory(),{},blocker_queue.primitive(),
      {{22,{{key(4097),shoo_action}}}});
  require(mover_queue.bind(mover)&&blocker_queue.bind(blocker),
          "two avatar queues bind independent VMs over one shared World");
  mover.start(key(4096),{1,1,1,0},{0});
  blocker.start(key(4096),{3,3,3,0},{0});
  require(blocker_queue.enqueue(key(4097),{3,2,2,22}),
          "blocker avatar receives its own queued action targeting the shared Destination");
  require(mover.run(10)==odh::VmStatus::returned_true&&blocker.run(10)==odh::VmStatus::yielded,
          "host runs mover Main and blocker action in separate VMs");
  require(blocker_queue.observe()&&blocker_queue.completion()->result&&
          world.read({key(4097),0,{3,2,2,22},{},{},odh::VmCallReturnMode::ordinary,0},4,25)==7,
          "blocker action changes the shared Destination through its own Caller context");
  require(mover_queue.tick(1)&&blocker_queue.tick(1)&&
          mover.run(10)==odh::VmStatus::returned_true&&blocker.run(10)==odh::VmStatus::returned_true,
          "host advances both independent queues at the same logical tick");
  require(mover_queue.size()==0&&blocker_queue.size()==0&&world.find(2)->object_words.at(25)==7,
          "per-avatar completion is isolated while World changes remain shared");
 }
 { // A TS1 action-tree wrapper can idle while the FIFO front remains active.
  odh::World world;world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);
  world.declare_object_word(1,8,16);odh::InteractionQueue queue(world,1);
  const auto action_idle=idle(0,1);
  odh::Vm vm({{key(4096),bhav({idle(1,1),set(2,99)})}},world.vm_memory(),{},queue.primitive(),
      {{22,{{key(4096),bhav({action_idle,set(1,42)})}}}});
  require(queue.bind(vm),"action-tree queue binds");
  vm.start(key(4096),{1,1,1,0},{0});
  require(queue.enqueue(key(4096),{1,2,2,22},{4}),"action-tree IdleForInput fixture starts");
  require(queue.references_target(2),"active interaction target remains protected until observed complete");
  require(queue.tick(1)&&vm.run(30)==odh::VmStatus::waiting&&queue.active()&&queue.waiting()&&
          vm.stack().back().arguments[0]==3,"action-tree IdleForInput waits without completing the active interaction");
  const auto action_wait_state=queue.snapshot();odh::InteractionQueue restored(world,1);
  require(restored.bind(vm)&&restored.restore_snapshot(action_wait_state),
          "active action-tree wait snapshot restores with its independent pending ticket");
  for(std::uint64_t tick=2;tick<5;++tick)
      require(queue.tick(tick)&&vm.status()==odh::VmStatus::waiting&&vm.run(30)==odh::VmStatus::waiting,
              "action-tree idle deadline does not wake early just because its FIFO front is active");
  require(queue.tick(5)&&!queue.waiting()&&vm.run(30)==odh::VmStatus::yielded&&
          vm.temps()[1]==42,"action-tree IdleForInput resumes at its countdown deadline and finishes the action");
  require(queue.observe()&&queue.completion()&&queue.completion()->result&&
          !queue.active()&&queue.tick(6)&&vm.run(30)==odh::VmStatus::returned_true,
          "Main resumes on the following tick after action-tree idle and successful completion");
  require(!queue.references_target(2),"completed interaction releases its target for deferred object removal");
 }
 { // AllowPush inside an action protects its parent and runs the higher-priority child above it.
  for(const bool child_result:{true,false}){
   odh::World world;world.create(1,odh::EntityKind::avatar,{0,70,0,0});
   world.create(2,odh::EntityKind::object);world.declare_object_word(1,8,0);
   world.set_person_data(1,33,0);
   odh::InteractionQueue queue(world,1,4);
   const odh::RoutineSet main{{key(4096),bhav({idle(1,1),set(2,99)})}};
   const auto child=child_result?bhav({set(1,33)}):bhav({set(1,33,255)});
   const odh::RoutineOwners owners{{22,{{key(4096),bhav({set(3,77,1),idle(1,1),set(0,22)})},
                                       {key(4097),child}}}};
   odh::Vm vm(main,world.vm_memory(),{},queue.primitive(),owners);
   require(queue.bind(vm),"nested AllowPush queue binds");vm.start(key(4096),{1,1,1,0},{4});
   require(queue.enqueue_ts1(key(4096),{1,2,2,22},{4},40,odh::InteractionQueueMode::normal,0,false,false),
       "outer action enters ahead of idle Main");
   require(vm.run(20)==odh::VmStatus::waiting&&queue.active()&&queue.size()==1&&
       vm.stack().size()==2&&vm.stack()[1].key.id==4096&&vm.temps()[3]==77,
       "outer action reaches its own AllowPush wait with its frame and registers intact");
   const auto parent_uid=queue.snapshot().active->uid;
   require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false),
       "higher-priority action queues behind the protected parent");
   require(queue.tick(1)&&vm.run(20)==odh::VmStatus::yielded,
       "parent IdleForInput hands control to the queued child");
   const auto child_state=queue.snapshot();
   require(child_state.suspended_active.size()==1&&child_state.active&&child_state.actions.size()==2&&
       child_state.actions[0].uid==parent_uid&&child_state.suspended_active[0].uid==parent_uid&&
       child_state.actions[1].uid==child_state.active->uid&&vm.interaction_return()&&
       vm.interaction_return()->ticket==child_state.active->ticket&&
       vm.interaction_return()->result==child_result&&vm.stack().size()==2&&
       vm.stack()[1].key.id==4096&&vm.temps()[3]==77,
       "nested child starts above the protected parent");
   const auto parent_frame_after_child=vm.stack()[1];
   require(queue.observe()&&queue.completion()&&queue.completion()->uid==child_state.active->uid&&
       queue.completion()->result==child_result&&queue.active()&&queue.snapshot().active->uid==parent_uid&&
       queue.snapshot().resume_tick==2&&queue.snapshot().suspended_active.empty()&&
       world.find(1)->person_data[33]==40&&vm.stack()[1].key.id==parent_frame_after_child.key.id&&
       vm.stack()[1].pc==parent_frame_after_child.pc&&
       vm.stack()[1].arguments==parent_frame_after_child.arguments&&
       vm.stack()[1].context.caller==parent_frame_after_child.context.caller&&
       vm.stack()[1].context.callee==parent_frame_after_child.context.callee&&
       vm.stack()[1].context.code_owner==parent_frame_after_child.context.code_owner,
       "child completion promotes its parent and restores the parent priority");
   require(queue.tick(2)&&vm.run(20)==odh::VmStatus::waiting&&queue.active()&&
       queue.snapshot().active->uid==parent_uid&&vm.stack().size()==2&&vm.stack()[1].key.id==4096&&
       vm.stack()[1].context.code_owner==parent_frame_after_child.context.code_owner&&
       vm.temps()[3]==77&&vm.temps()[1]==33,
       "next tick resumes the same parent action with its VM registers preserved");
  }
 }
 { // Completing a child immediately reevaluates pending work against the restored parent priority.
  odh::World world;world.create(1,odh::EntityKind::avatar,{0,70,0,0});
  world.create(2,odh::EntityKind::object);world.declare_object_word(1,8,0);
  world.set_person_data(1,33,0);odh::InteractionQueue queue(world,1,4);
  const odh::RoutineSet main{{key(4096),bhav({idle(1,1),set(2,99)})}};
  const odh::RoutineOwners owners{{22,{{key(4096),bhav({idle(1,1),set(0,22)})},
                                      {key(4097),bhav({set(1,33)})}}}};
  odh::Vm vm(main,world.vm_memory(),{},queue.primitive(),owners);queue.bind(vm);
  vm.start(key(4096),{1,1,1,0},{4});
  require(queue.enqueue_ts1(key(4096),{1,2,2,22},{4},40,odh::InteractionQueueMode::normal,0,false,false)&&
      vm.run(20)==odh::VmStatus::waiting,"priority reevaluation fixture starts its parent action");
  require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false)&&
      queue.tick(1)&&vm.run(20)==odh::VmStatus::yielded,
      "priority reevaluation fixture starts its child action");
  require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},60,odh::InteractionQueueMode::normal,0,false,false),
      "higher-priority work remains pending behind the active child");
  require(queue.observe()&&queue.snapshot().active&&queue.snapshot().active->uid==queue.snapshot().actions[0].uid&&
      queue.snapshot().actions[0].notify_idle&&
      (static_cast<std::uint16_t>(world.find(1)->object_words.at(8))&0x0080U),
      "child completion restores the parent then immediately recalculates NotifyIdle and InteractionCanceled");
 }
 { // Cancellation separates source queue UID from native identity and runs the skip hook once.
  CancelFixture f;require(f.enqueue(4097,50),"pending action to cancel enqueues");
  const auto uid=f.queue.last_enqueued_uid();std::size_t callbacks=0;std::uint16_t callback_queue_uid=0;
  f.queue.set_queue_skipped_handler([&](const odh::InteractionActionSnapshot& action){
   ++callbacks;callback_queue_uid=action.queue_uid;return action.uid==uid&&action.call.key.id==4097;
  });
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::removed&&f.queue.size()==0&&
      callbacks==1&&callback_queue_uid==0&&!f.queue.completion(),
      "pending Normal cancellation removes the action and runs EntryPoint4 hook exactly once");
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::missing&&callbacks==1,
      "removed native identity cannot run the callback twice");
 }
 { // A stale target is not removed or passed to the callback.
  CancelFixture f;require(f.enqueue(4097,50),"stale-target action enqueues");
  const auto uid=f.queue.last_enqueued_uid();std::size_t callbacks=0;
  f.queue.set_queue_skipped_handler([&](const odh::InteractionActionSnapshot&){++callbacks;return true;});
  f.world.remove(2);f.world.create(2,odh::EntityKind::object);
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::unsupported&&f.queue.size()==1&&
      f.queue.contains_uid(uid)&&callbacks==0,
      "stale target cancellation preserves the request and never dispatches EntryPoint4");
 }
 { // Without a synchronous skip hook, cancellation leaves a pending action untouched.
  CancelFixture f;require(f.enqueue(4097,50),"no-hook action enqueues");
  const auto uid=f.queue.last_enqueued_uid();
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::unsupported&&f.queue.size()==1&&
      f.queue.contains_uid(uid),"missing EntryPoint4 handler cannot silently discard a Normal action");
 }
 { // Callback failure is reported after source-ordered removal, without a fake completion.
  CancelFixture f;require(f.enqueue(4097,50),"false-hook action enqueues");
  const auto uid=f.queue.last_enqueued_uid();std::size_t callbacks=0;
  f.queue.set_queue_skipped_handler([&](const odh::InteractionActionSnapshot&){++callbacks;return false;});
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::unsupported&&f.queue.size()==0&&
      callbacks==1&&!f.queue.completion()&&f.queue.cancel(uid)==odh::InteractionCancelResult::missing&&callbacks==1,
      "false EntryPoint4 hook does not resurrect or complete the removed action");
 }
 { // A throwing EntryPoint4 hook also reports post-removal failure exactly once.
  CancelFixture f;require(f.enqueue(4097,50),"throwing-hook action enqueues");
  const auto uid=f.queue.last_enqueued_uid();std::size_t callbacks=0;
  f.queue.set_queue_skipped_handler([&](const odh::InteractionActionSnapshot&)->bool{
   ++callbacks;throw std::runtime_error("fixture EntryPoint4 failure");
  });
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::unsupported&&f.queue.size()==0&&
      callbacks==1&&!f.queue.completion()&&f.queue.cancel(uid)==odh::InteractionCancelResult::missing&&callbacks==1,
      "throwing EntryPoint4 hook does not resurrect or complete the removed action");
 }
 { // The synchronous hook cannot recursively cancel another item in the same queue.
  CancelFixture f;require(f.enqueue(4097,50)&&f.enqueue(4098,40),"reentrant-hook actions enqueue");
  const auto first=f.queue.snapshot().actions[0].uid,second=f.queue.snapshot().actions[1].uid;
  odh::InteractionCancelResult nested=odh::InteractionCancelResult::missing;
  f.queue.set_queue_skipped_handler([&](const odh::InteractionActionSnapshot& action){
   if(action.uid==first)nested=f.queue.cancel(second)==odh::InteractionCancelResult::unsupported?
       odh::InteractionCancelResult::unsupported:odh::InteractionCancelResult::removed;
   return true;
  });
  require(f.queue.cancel(first)==odh::InteractionCancelResult::removed&&
      nested==odh::InteractionCancelResult::unsupported&&f.queue.size()==1&&f.queue.contains_uid(second),
      "same-queue recursive cancellation is rejected without changing the other action");
 }
 { // MustRun remains queued, notifying IdleForInput and preserving unrelated flag bits.
  CancelFixture f;require(f.enqueue(4097,50,odh::InteractionQueueMode::normal,1U<<10),
      "MustRun action enqueues");
  const auto uid=f.queue.last_enqueued_uid();
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::notified&&f.queue.size()==1&&
      f.queue.snapshot().actions[0].notify_idle&&f.world.find(1)->person_data[33]==0&&
      (static_cast<std::uint16_t>(f.world.find(1)->object_words.at(8))&0x0090U)==0x0090U,
      "MustRun cancellation retains the item and sets InteractionCanceled plus priority zero");
  odh::EntityThreadControllersSnapshot saved{};saved.interactions=f.queue.snapshot();
  const auto restored=odh::decode_entity_thread_controllers_snapshot(
      odh::encode_entity_thread_controllers_snapshot(saved));
  require(restored.interactions.actions.size()==1&&restored.interactions.actions[0].uid==uid&&
      restored.interactions.actions[0].queue_uid==saved.interactions.actions[0].queue_uid&&
      restored.interactions.actions[0].flags==saved.interactions.actions[0].flags&&
      (restored.interactions.actions[0].flags&(1U<<10))&&restored.interactions.actions[0].notify_idle,
      "retained cancellation metadata and wrapping source UID survive snapshot roundtrip");
 }
 { // Active and protected actions are notified rather than erased.
  CancelFixture f;require(f.enqueue(4097,50)&&f.vm.run(1)==odh::VmStatus::budget_exhausted&&
      f.queue.active(),"active cancellation fixture dispatches its action");
  const auto uid=f.queue.snapshot().active->uid;
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::notified&&f.queue.active()&&f.queue.size()==1&&
      f.queue.snapshot().actions[0].notify_idle&&f.world.find(1)->person_data[33]==0&&
      (static_cast<std::uint16_t>(f.world.find(1)->object_words.at(8))&0x0080U),
      "active cancellation preserves the protected action and requests cooperative interruption");
 }
 { // DirectControl may be removed even when its queue mode is not Normal.
  CancelFixture f;require(f.enqueue(4097,30,odh::InteractionQueueMode::parent_exit,1U<<27),
      "DirectControl ParentExit action enqueues");
  const auto uid=f.queue.last_enqueued_uid();std::size_t callbacks=0;
  f.queue.set_queue_skipped_handler([&](const odh::InteractionActionSnapshot& action){
   ++callbacks;return action.uid==uid;
  });
  require(f.queue.cancel(uid)==odh::InteractionCancelResult::removed&&f.queue.size()==0&&callbacks==1,
      "non-MustRun DirectControl can be removed despite ParentExit mode");
 }
 { // Cancelling ParentIdle removes following ParentIdle entries and hides ParentExit at priority zero.
  CancelFixture f;
  require(f.enqueue(4097,40,odh::InteractionQueueMode::parent_idle,false),"ParentIdle cancel target enqueues");
  const auto parent_uid=f.queue.last_enqueued_uid();
  require(f.enqueue(4098,30,odh::InteractionQueueMode::parent_exit),"ParentExit followup enqueues");
  const auto exit_uid=f.queue.last_enqueued_uid();
  require(f.enqueue(4097,40,odh::InteractionQueueMode::parent_idle,0,true),"ParentIdle followup enqueues");
  const auto idle_uid=f.queue.last_enqueued_uid();
  require(f.queue.cancel(parent_uid)==odh::InteractionCancelResult::notified&&f.queue.size()==2&&
      f.queue.snapshot().actions[0].uid==parent_uid&&f.queue.snapshot().actions[0].notify_idle&&
      f.queue.snapshot().actions[1].uid==exit_uid&&f.queue.snapshot().actions[1].notify_idle&&
      f.queue.snapshot().actions[1].priority==0&&!f.queue.contains_uid(idle_uid)&&
      f.world.find(1)->person_data[33]==0,
      "ParentIdle chain removes its pending ParentIdle child but preserves ParentExit as cancelled");
 }
 { // ParentIdle cannot remove a protected nested descendant or partially signal cancellation.
  odh::World world;world.create(1,odh::EntityKind::avatar,{0,70,0,0});
  world.create(2,odh::EntityKind::object);world.declare_object_word(1,8,16);world.set_person_data(1,33,0);
  odh::InteractionQueue queue(world,1,4);
  const odh::RoutineSet main{{key(4096),bhav({idle(1,1),set(2,99)})}};
  const odh::RoutineOwners owners{{22,{{key(4096),bhav({set(3,77,1),idle(1,1),set(0,22)})},
                                      {key(4097),bhav({set(1,33)})}}}};
  odh::Vm vm(main,world.vm_memory(),{},queue.primitive(),owners);queue.bind(vm);
  vm.start(key(4096),{1,1,1,0},{4});
  // Both actions exist before dispatch; PushHead puts the lower-priority
  // parent first. Its commit clears the cancel bit, allowing ParentIdle nesting
  // in the same VM slice before a tick-boundary priority notification.
  require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::parent_idle,
          0,false,false)&&queue.enqueue_ts1(key(4096),{1,2,2,22},{4},40,
          odh::InteractionQueueMode::parent_idle,0,true,false),"ParentIdle protected chain queues before dispatch");
  const auto parent_uid=queue.last_enqueued_uid();
  require(vm.run(20)==odh::VmStatus::yielded&&
          queue.snapshot().suspended_active.size()==1&&queue.active(),
      "ParentIdle child becomes a protected nested descendant");
  const auto before=queue.snapshot();const auto priority=world.find(1)->person_data[33];
  const auto flags=world.find(1)->object_words.at(8);
  require(queue.cancel(parent_uid)==odh::InteractionCancelResult::unsupported&&
      queue.snapshot().actions.size()==before.actions.size()&&
      queue.snapshot().actions[0].uid==before.actions[0].uid&&
      queue.snapshot().actions[1].uid==before.actions[1].uid&&
      queue.snapshot().actions[0].notify_idle==before.actions[0].notify_idle&&
      queue.snapshot().actions[1].notify_idle==before.actions[1].notify_idle&&
      queue.snapshot().suspended_active.size()==before.suspended_active.size()&&
      world.find(1)->person_data[33]==priority&&world.find(1)->object_words.at(8)==flags,
      "protected ParentIdle descendant rejection leaves queue and cancellation flags unchanged");
 }
 { // Internal cancel uses the 64-bit identity even after the source UID wraps.
  CancelFixture f;auto snapshot=f.queue.snapshot();snapshot.next_queue_uid=0xffffU;
  require(f.queue.restore_snapshot(snapshot)&&f.enqueue(4097,50)&&f.enqueue(4098,50),
      "queue UID wrap fixture restores and queues two actions");
  const auto state=f.queue.snapshot();const auto first=state.actions[0],second=state.actions[1];
  require(first.queue_uid==0xffffU&&second.queue_uid==0&&first.uid!=second.uid,
      "public queue UID wraps independently from native identity");
  std::uint16_t skipped_source_uid=0xffffU;
  f.queue.set_queue_skipped_handler([&](const odh::InteractionActionSnapshot& action){
   skipped_source_uid=action.queue_uid;return true;
  });
  require(f.queue.cancel(second.uid)==odh::InteractionCancelResult::removed&&f.queue.size()==1&&
      f.queue.contains_uid(first.uid)&&!f.queue.contains_uid(second.uid)&&skipped_source_uid==0,
      "native cancellation selects the correct action across source UID wrap");
 }
 { // A legacy FIFO snapshot keeps its old profile instead of silently gaining TS1 cancellation.
  CancelFixture f;require(f.enqueue(4097,50),"legacy cancellation fixture action enqueues");
  auto saved=f.queue.snapshot();saved.profile=odh::InteractionQueueProfile::legacy_fifo;
  odh::InteractionQueue restored(f.world,1,8);odh::Vm restored_vm(
      {{key(4096),bhav({idle(1,1),set(2,99)})}},f.world.vm_memory(),{},restored.primitive(),
      {{22,{{key(4097),bhav({set(0,33)})},{key(4098),bhav({set(1,44)})}}}});
  restored.bind(restored_vm);restored_vm.start(key(4096),{1,1,1,0},{4});
  require(restored.restore_snapshot(saved)&&restored.cancel(saved.actions[0].uid)==
      odh::InteractionCancelResult::unsupported&&restored.size()==1,
      "legacy FIFO restore rejects TS1 cancellation without changing the queue");
 }
 { // Ordinary insertion cancels the original lower-priority suffix in source order.
  for(const auto incoming:{std::int16_t{50},std::int16_t{100}}){
   CancelFixture f;std::vector<std::uint64_t> skipped;
   require(f.enqueue(4097,100)&&f.enqueue(4098,10,odh::InteractionQueueMode::normal,0,true)&&
       f.enqueue(4097,5,odh::InteractionQueueMode::normal,0,true),"automatic-skip suffix setup uses tail insertion");
   const auto before=f.queue.snapshot();const auto next=before.sequence+1;
   bool published=true;
   f.queue.set_queue_skipped_handler([&](const auto& action){
    skipped.push_back(action.uid);published=published&&f.queue.last_enqueued_uid()==next&&f.queue.contains_uid(next);return true;
   });
   require(f.enqueue(4098,incoming)&&skipped==std::vector<std::uint64_t>({before.actions[1].uid,before.actions[2].uid})&&published,
       "ordinary high/equal-priority pivot skips each original suffix item after publishing the new identity");
   const auto after=f.queue.snapshot();
   require(after.actions.size()==2&&after.actions[0].uid==before.actions[0].uid&&after.actions[1].uid==next&&
       after.actions[1].queue_uid==before.next_queue_uid&&after.next_queue_uid==before.next_queue_uid+1&&!f.queue.completion(),
       "automatic skip retains the pivot and new request without fabricating completion");
  }
 }
 { // Head and tail use their own insertion branches even when a pivot-like order exists.
  for(const auto flags:{1U<<29,1U<<30,(1U<<29)|(1U<<30)}){
   CancelFixture f;std::size_t callbacks=0;
   require(f.enqueue(4097,100)&&f.enqueue(4098,10,odh::InteractionQueueMode::normal,0,true)&&
       f.enqueue(4097,5,odh::InteractionQueueMode::normal,0,true),"head/tail bypass suffix setup");
   f.queue.set_queue_skipped_handler([&](const auto&){++callbacks;return true;});
   require(f.enqueue(4098,50,odh::InteractionQueueMode::normal,flags)&&f.queue.size()==4&&callbacks==0,
       "PushHead and PushTail never invoke ordinary automatic suffix cancellation");
   const auto after=f.queue.snapshot();
   require(after.actions[(flags&(1U<<29))?0:1].uid==f.queue.last_enqueued_uid(),
       "head takes precedence when both insertion flags are present");
  }
 }
 { // A ParentExit boundary alone does not enable skipping when its priority is lower.
  CancelFixture f;std::size_t callbacks=0;
  require(f.enqueue(4097,30,odh::InteractionQueueMode::parent_exit)&&
      f.enqueue(4098,10,odh::InteractionQueueMode::normal,0,true),"ParentExit-only pivot setup");
  f.queue.set_queue_skipped_handler([&](const auto&){++callbacks;return true;});
  require(f.enqueue(4097,50)&&f.queue.size()==3&&callbacks==0&&f.queue.snapshot().actions[1].priority==50,
      "finding ParentExit with incoming priority above pivot inserts without cancelling its suffix");
 }
 { // Retained MustRun is signalled once; the pass must continue rather than spin on it.
  CancelFixture f;std::vector<std::uint64_t> skipped;
  require(f.enqueue(4097,100)&&f.enqueue(4098,10,odh::InteractionQueueMode::normal,1U<<10,true)&&
      f.enqueue(4097,5,odh::InteractionQueueMode::normal,0,true),"MustRun retained suffix setup");
  const auto before=f.queue.snapshot();
  f.queue.set_queue_skipped_handler([&](const auto& action){skipped.push_back(action.uid);return true;});
  require(f.enqueue(4098,50)&&skipped==std::vector<std::uint64_t>({before.actions[2].uid}),
      "bounded automatic skip passes retained MustRun and removes the next original Normal entry once");
  const auto after=f.queue.snapshot();
  require(after.actions.size()==3&&after.actions[2].uid==before.actions[1].uid&&after.actions[2].notify_idle&&
      (after.actions[2].flags&(1U<<10))&&f.world.find(1)->person_data[33]==0&&
      (f.world.find(1)->object_words.at(8)&128),"retained MustRun cancellation state is preserved");
  odh::EntityThreadControllersSnapshot saved{};saved.interactions=after;
  const auto decoded=odh::decode_entity_thread_controllers_snapshot(odh::encode_entity_thread_controllers_snapshot(saved));
  odh::InteractionQueue restored(f.world,1,8);
  odh::Vm restored_vm({{key(4096),bhav({idle(1,1),set(2,99)})}},f.world.vm_memory(),{},restored.primitive(),
      {{22,{{key(4097),bhav({set(0,33)})},{key(4098),bhav({set(1,44)})}}}});
  require(restored_vm.restore_snapshot(f.vm.snapshot())&&restored.bind(restored_vm)&&
      restored.restore_snapshot(decoded.interactions)&&restored.snapshot().actions[2].notify_idle&&
      restored.snapshot().actions[2].uid==before.actions[1].uid&&restored.snapshot().sequence==after.sequence&&
      restored.snapshot().next_queue_uid==after.next_queue_uid,
      "bounded suffix cancellation metadata restores against its exact Main VM");
 }
 { // ParentIdle can erase a later original UID; it must not be visited or called again.
  CancelFixture f;std::vector<std::uint64_t> skipped;
  require(f.enqueue(4097,100)&&f.enqueue(4098,30,odh::InteractionQueueMode::parent_exit)&&
      f.enqueue(4097,10,odh::InteractionQueueMode::parent_idle,0,true)&&
      f.enqueue(4098,5,odh::InteractionQueueMode::parent_idle,0,true)&&
      f.enqueue(4097,1,odh::InteractionQueueMode::normal,0,true),"ParentIdle erased-suffix setup");
  const auto before=f.queue.snapshot();
  f.queue.set_queue_skipped_handler([&](const auto& action){skipped.push_back(action.uid);return true;});
  require(f.enqueue(4098,20)&&f.queue.size()==4&&
      !f.queue.contains_uid(before.actions[3].uid)&&!f.queue.contains_uid(before.actions[4].uid)&&
      skipped==std::vector<std::uint64_t>({before.actions[4].uid})&&
      f.queue.snapshot().actions.back().uid==before.actions[2].uid&&f.queue.snapshot().actions.back().notify_idle,
      "ParentIdle removal skips the erased UID and still cancels the later Normal exactly once");
 }
 { // Active prefix cannot serve as an ordinary insertion pivot or be cancelled by it.
  CancelFixture f;std::size_t callbacks=0;
  require(f.enqueue(4097,100)&&f.vm.run(1)==odh::VmStatus::budget_exhausted&&f.queue.active(),
      "protected-prefix ordinary insertion starts an active action");
  const auto active=f.queue.snapshot().active;
  require(f.enqueue(4098,10,odh::InteractionQueueMode::normal,0,true)&&
      f.enqueue(4097,5,odh::InteractionQueueMode::normal,0,true),"protected-prefix pending suffix setup");
  f.queue.set_queue_skipped_handler([&](const auto&){++callbacks;return true;});
  require(f.enqueue(4098,50)&&callbacks==0&&f.queue.size()==4&&f.queue.snapshot().active->uid==active->uid&&
      f.queue.snapshot().suspended_active.empty()&&f.queue.snapshot().actions[1].priority==50,
      "no pending pivot means insert after protected prefix without suffix cancellation");
 }
 { // A failed later callback cannot undo earlier removal/World effects or reuse the rejected UID.
  CancelFixture f;std::vector<std::uint64_t> skipped;
  f.world.declare_object_word(2,25,0);
  require(f.enqueue(4097,100)&&f.enqueue(4098,10,odh::InteractionQueueMode::normal,0,true)&&
      f.enqueue(4097,5,odh::InteractionQueueMode::normal,0,true),"partial automatic-skip failure setup");
  const auto before=f.queue.snapshot();const auto rejected=before.sequence+1;
  f.queue.set_queue_skipped_handler([&](const auto& action){
   skipped.push_back(action.uid);f.world.declare_object_word(2,25,static_cast<std::int16_t>(skipped.size()));return skipped.size()<2;
  });
  require(!f.enqueue(4098,50)&&skipped.size()==2&&f.queue.size()==1&&
      f.queue.contains_uid(before.actions[0].uid)&&!f.queue.contains_uid(rejected)&&
      f.queue.snapshot().sequence==rejected&&f.queue.snapshot().next_queue_uid==before.next_queue_uid+1&&
      f.world.find(2)->object_words.at(25)==2&&!f.queue.completion()&&
      f.queue.diagnostic().find("earlier effects retained")!=std::string::npos,
      "callback failure removes only the new request while retaining prior irreversible effects and consumed UID");
  require(f.enqueue(4098,1,odh::InteractionQueueMode::normal,1U<<29)&&
      f.queue.last_enqueued_uid()==rejected+1&&f.queue.snapshot().actions.front().queue_uid==before.next_queue_uid+1,
      "next accepted request cannot reuse failed published native or public identity");
 }
 { // EntryPoint4 effects can invalidate the new target after the old suffix was removed.
  CancelFixture f;require(f.world.create(3,odh::EntityKind::object)&&f.enqueue(4097,100)&&
      f.enqueue(4098,10,odh::InteractionQueueMode::normal,0,true),"new target callback invalidation setup");
  const auto before=f.queue.snapshot();
  f.queue.set_queue_skipped_handler([&](const auto&){return f.world.remove(3);});
  require(!f.queue.enqueue_ts1(key(4097),{1,3,3,22},{},50,odh::InteractionQueueMode::normal,0,false,false)&&
      f.queue.size()==1&&f.queue.contains_uid(before.actions[0].uid)&&
      !f.queue.contains_uid(before.sequence+1)&&!f.world.find(3)&&f.queue.snapshot().sequence==before.sequence+1&&
      f.queue.diagnostic().find("invalidated the new request")!=std::string::npos,
      "callback World effects cannot publish a new request to a target that disappeared");
 }
 { // Missing skip support rejects the new insertion, leaving the original suffix intact.
  CancelFixture f;
  require(f.enqueue(4097,100)&&f.enqueue(4098,10,odh::InteractionQueueMode::normal,0,true),
      "automatic-skip no-hook setup");
  const auto before=f.queue.snapshot();
  require(!f.enqueue(4097,50)&&f.queue.size()==2&&
      f.queue.snapshot().actions[0].uid==before.actions[0].uid&&f.queue.snapshot().actions[1].uid==before.actions[1].uid&&
      f.queue.snapshot().sequence==before.sequence+1&&!f.queue.contains_uid(before.sequence+1),
      "missing EntryPoint4 hook does not silently accept a request whose suffix could not be cancelled");
 }
 { // An inactive TS1 queue has ActiveQueueBlock=-1, so head inserts at zero.
  Fixture f(1,3);require(f.enqueue()&&f.enqueue(4097),"inactive PushHead fixture queues ordinary actions");
  require(f.queue.enqueue(key(4096),{1,2,2,22},{},true),"inactive PushHead accepts a continuation");
  const auto snapshot=f.queue.snapshot();
  require(!snapshot.active&&snapshot.actions.size()==3&&snapshot.actions[0].uid==3&&
      snapshot.actions[1].uid==1&&snapshot.actions[2].uid==2&&
      snapshot.actions[0].call.context.callee==2&&snapshot.actions[0].call.key.id==4096,
      "inactive queue orders head C before pending A and B with exact action context");
 }
 { // TS1 priority ordering applies to the real interaction queue and protects its active front.
  Fixture f(1,8);require(f.enqueue(),"priority ordering begins with an inherited action");
  std::size_t skipped=0;f.queue.set_queue_skipped_handler([&](const auto&){++skipped;return true;});
  require(f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false)&&
          f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},30,odh::InteractionQueueMode::normal,0,false,false)&&
          f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},2,odh::InteractionQueueMode::normal,0,false,true),
          "priority and tail actions enter the same live TS1 queue");
  auto state=f.queue.snapshot();
  require(state.actions.size()==3&&state.actions[0].priority==50&&state.actions[1].priority==30&&
          state.actions[2].priority==2&&skipped==1,
          "ordinary insertion skips the inherited suffix while PushTail retains the remaining priority order");
  require(f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},0,odh::InteractionQueueMode::normal,0,true,false),
          "PushHead action enters the TS1 queue");
  state=f.queue.snapshot();
  require(state.actions.front().priority==0&&state.actions.back().priority==2&&
          state.actions.front().flags==(1U<<28|1U<<29),
          "PushHead ignores priority and records skip-permission and queue flags");
 }
 { // Parent modes partition ordinary insertion; RunImmediately appends in source Idle mode.
  Fixture f(1,8);
  require(f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},30,odh::InteractionQueueMode::parent_exit,0,false,false)&&
          f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},40,odh::InteractionQueueMode::parent_idle,0,false,false)&&
          f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},20,odh::InteractionQueueMode::normal,0,false,false)&&
          f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false),
          "ParentIdle, ParentExit and ordinary actions enter the same queue");
  const auto state=f.queue.snapshot();
  require(state.actions.size()==4&&state.actions[0].mode==odh::InteractionQueueMode::parent_idle&&
          state.actions[1].mode==odh::InteractionQueueMode::parent_exit&&
          state.actions[2].priority==50&&state.actions[3].priority==20,
          "ParentExit forms an insertion boundary while ParentIdle starts before its parent group");
  const auto before=f.queue.snapshot();
  require(f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},100,odh::InteractionQueueMode::normal,1U<<2,true,true)&&
          f.queue.snapshot().actions.size()==before.actions.size()+1&&
          f.queue.snapshot().actions.back().uid==before.sequence+1&&
          f.queue.snapshot().actions.back().mode==odh::InteractionQueueMode::idle&&
          f.queue.snapshot().actions.back().call.return_mode==odh::VmCallReturnMode::immediate&&
          f.queue.snapshot().actions.front().uid==before.actions.front().uid,
          "RunImmediately appends in hidden Idle mode despite head/tail flags without cancelling prior actions");
 }
 { // A higher-priority TS1 action notifies the protected action at its scheduled IdleForInput.
  odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});world.create(2,odh::EntityKind::object);
  world.declare_object_word(1,8,0);odh::InteractionQueue queue(world,1);
  odh::Vm vm({{key(4096),bhav({idle(1,1),set(2,99)})}},world.vm_memory(),{},queue.primitive(),
      {{22,{{key(4097),bhav({idle(0,1),set(1,42)})}}}});
  require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0},{8})==odh::VmStatus::running,
      "priority-notification avatar has full TS1 PersonData and a live Main");
  require(queue.enqueue_ts1(key(4097),{1,2,2,22},{2},10,odh::InteractionQueueMode::normal,0,false,false),
      "priority action enters queue");
  require(queue.tick(1),"priority action tick advances");
  require(vm.run(20)==odh::VmStatus::waiting,"priority action reaches its idle wait");
  require(queue.active()&&queue.waiting()&&world.find(1)->person_data[33]==10,
      "starting an action publishes its live priority before its action tree waits");
  require(queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false),
      "higher-priority work joins behind the protected action");
  auto state=queue.snapshot();
  require(state.actions.front().notify_idle&&(static_cast<std::uint16_t>(world.find(1)->object_words.at(8))&128U),
      "queue reevaluation marks the active action and InteractionCanceled");
  require(queue.tick(2)&&vm.run(20)==odh::VmStatus::waiting,
      "AllowPush0 keeps its scheduled wake instead of polling each tick");
  require(queue.tick(3)&&vm.run(20)==odh::VmStatus::yielded&&vm.temps()[1]==42&&
      (static_cast<std::uint16_t>(world.find(1)->object_words.at(8))&64U),
      "scheduled IdleForInput sees NotifyIdle and publishes NotifiedByIdleForInput");
  require(queue.observe()&&world.find(1)->person_data[33]==0&&queue.size()==1,
      "finishing the outer action resets priority and leaves the higher-priority request pending");
 }
 { // Setting the active TS1 priority to zero also notifies that action at the next tick.
  odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});world.create(2,odh::EntityKind::object);
  world.declare_object_word(1,8,0);odh::InteractionQueue queue(world,1);
  odh::Vm vm({{key(4096),bhav({idle(1,1),set(2,99)})}},world.vm_memory(),{},queue.primitive(),
      {{22,{{key(4097),bhav({idle(0,1),set(1,42)})}}}});
  require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0},{})==odh::VmStatus::running&&
      queue.enqueue_ts1(key(4097),{1,2,2,22},{2},10,odh::InteractionQueueMode::normal,0,false,false)&&
      queue.tick(1)&&vm.run(20)==odh::VmStatus::waiting,"priority-zero notification fixture starts a waiting action");
  require(world.set_person_data(1,33,0)&&queue.tick(2)&&vm.run(20)==odh::VmStatus::waiting&&
      queue.tick(3)&&vm.run(20)==odh::VmStatus::yielded&&
      (static_cast<std::uint16_t>(world.find(1)->object_words.at(8))&64U),
      "active action sees a priority-zero change at its scheduled wake without a queued replacement");
 }
 { // A full VM stack rejects dispatch before queue or PersonData claims an active action.
  odh::World world;world.create(1,odh::EntityKind::avatar,{0,80,0,16});world.create(2,odh::EntityKind::object);
  world.declare_object_word(1,8,0);odh::InteractionQueue queue(world,1);odh::VmLimits limits;limits.stack_depth=1;
  odh::Vm vm({{key(4096),bhav({idle(1,1),set(2,99)})}},world.vm_memory(),limits,queue.primitive(),
      {{22,{{key(4097),bhav({set(1,42)})}}}});
  require(queue.bind(vm)&&vm.start(key(4096),{1,1,1,0},{})==odh::VmStatus::running&&
      queue.enqueue_ts1(key(4097),{1,2,2,22},{},10,odh::InteractionQueueMode::normal,0,false,false)&&
      vm.run(10)==odh::VmStatus::fault&&!queue.active()&&queue.size()==1&&
      world.find(1)->person_data[33]==0,
      "stack-depth rejection leaves the request pending and live priority unchanged");
 }
 { // A v1 FIFO save adopts TS1 ordering only when new native PushInteraction work arrives.
  Fixture f(1,8);require(f.enqueue()&&f.enqueue(4097),"legacy migration begins with two queued actions");
  auto legacy=f.queue.snapshot();legacy.profile=odh::InteractionQueueProfile::legacy_fifo;
  legacy.next_queue_uid=0;
  for(auto& action:legacy.actions){action.queue_uid=0;action.priority=1;action.mode=odh::InteractionQueueMode::normal;action.flags=0;}
  odh::InteractionQueue restored(f.world,1,8);
  require(restored.bind(f.vm)&&restored.restore_snapshot(legacy),"legacy FIFO state restores before migration");
  require(restored.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false),
          "new PushInteraction upgrades the restored FIFO queue");
  const auto migrated=restored.snapshot();
  require(migrated.profile==odh::InteractionQueueProfile::ts1_priority&&migrated.actions.size()==3&&
          migrated.actions[0].priority==50&&migrated.actions[1].uid==legacy.actions[0].uid&&
          migrated.actions[2].uid==legacy.actions[1].uid&&migrated.actions[0].queue_uid==0&&
          migrated.actions[1].queue_uid==1&&migrated.actions[2].queue_uid==2&&migrated.next_queue_uid==3,
          "legacy action order survives adoption and public queue IDs become unique");
 }
 { // PushHead keeps the running action first but may reorder later UIDs.
  Fixture f(1,3);require(f.enqueue()&&f.enqueue(4097),"PushHead fixture queues two actions");
  require(f.queue.references_target(2),"queue reports active or pending targets for safe deferred deletion");
  require(f.vm.run(1)==odh::VmStatus::budget_exhausted&&f.queue.active(),
      "PushHead fixture starts the first action");
  require(f.queue.enqueue(key(4097),{1,2,2,22},{},true),
      "PushHead continuation inserts while another action is active");
  const auto snapshot=f.queue.snapshot();
  require(snapshot.actions.size()==3&&snapshot.actions[0].uid==1&&
      snapshot.actions[1].uid==3&&snapshot.actions[2].uid==2,
      "PushHead continuation runs after active action and before older pending work");
  odh::InteractionQueue restored(f.world,1,3);
  require(restored.bind(f.vm)&&restored.restore_snapshot(snapshot),
      "reordered interaction queue snapshot restores against the active action");
 }
 { // A named BHAV can change code owner while remaining part of the active action.
  odh::World world;world.create(1,odh::EntityKind::avatar);world.create(2,odh::EntityKind::object);
  world.declare_object_word(1,8,16);odh::InteractionQueue queue(world,1);
  const odh::Instruction run_named{28,1,1,{0,0,0,0,0,0,0,0}};
  const odh::VmPrimitive nested=[&](const odh::VmFrame& frame,const odh::Instruction& instruction,
      const std::array<std::int16_t,20>& temps,const odh::VmDispatch& dispatch){
   if(instruction.opcode==28){
    auto context=odh::VmContext{1,2,2,33};
    return odh::PrimitiveOutcome{odh::PrimitiveDisposition::call,{}, {},
        odh::VmCall{key(4097),context,{4}}};
   }
   return queue.primitive()(frame,instruction,temps,dispatch);
  };
  odh::Vm vm({{key(4098),bhav({idle(1,1),set(2,0)})}},world.vm_memory(),{},nested,
      {{22,{{key(4096),bhav({run_named,set(2,0)})}}},
       {33,{{key(4097),bhav({idle(0,1),set(1,42)})}}}});
  require(queue.bind(vm),"named action fixture binds");vm.start(key(4098),{1,1,1,0},{0});
  const bool named_enqueued=queue.enqueue(key(4096),{1,2,2,22});
  if(!named_enqueued)std::cerr<<"named action enqueue: "<<queue.diagnostic()<<" vm="<<odh::vm_status_name(vm.status())<<" ("<<vm.diagnostic()<<")\n";
  require(named_enqueued,"named action enters FIFO");
  require(queue.tick(1)&&vm.run(30)==odh::VmStatus::waiting&&queue.active()&&queue.waiting()&&
      vm.stack().back().key.id==4097&&vm.stack().back().context.code_owner==33,
      "nested named BHAV IdleForInput waits inside the active action after code owner changes");
  for(std::uint64_t tick=2;tick<5;++tick)
   require(queue.tick(tick)&&vm.run(30)==odh::VmStatus::waiting,"nested named action keeps its full IdleForInput delay");
  require(queue.tick(5)&&vm.run(30)==odh::VmStatus::yielded&&vm.temps()[1]==42,
      "nested named action resumes and returns to its queue owner");
  require(queue.observe()&&queue.completion()&&queue.completion()->result,
      "nested named action completion is recorded");
 }
 {Fixture f;require(f.world.tick_motive_changes()&&f.world.set_motive_change(1,6,1800,100)&&
                     f.world.has_motive_change(1,6),"fixture has an active TS1 motive change");
  require(f.enqueue()&&f.enqueue(4097)&&!f.enqueue(),"FIFO capacity includes active front");
  require(f.vm.run(10)==odh::VmStatus::yielded&&f.vm.temps()[0]==22,"owner22 action executes over preserved owner0 Main");
  require(f.vm.stack().size()==1&&f.vm.stack()[0].pc==0&&f.vm.stack()[0].arguments[0]==-1,"decrement before push and unchanged Main pc after return");
  require(f.queue.observe()&&!f.queue.active()&&f.queue.size()==1&&f.queue.completion()->result&&
          !f.world.has_motive_change(1,6),"interaction completion removes the action's motive change");
  require(f.vm.run(10)==odh::VmStatus::yielded&&f.vm.temps()[2]==0,"same tick cannot resume Main");
  require(f.queue.tick(0)&&f.vm.status()==odh::VmStatus::yielded,"tick zero does not consume next-tick boundary");
  require(f.queue.tick(1)&&f.vm.run(10)==odh::VmStatus::yielded&&f.vm.temps()[1]==33,"next tick reexecutes AllowPush and invokes FIFO second action");
  require(f.queue.observe()&&f.queue.size()==0&&!f.queue.completion()->result,"false action result recorded without branching Main false");
  require(!f.queue.tick(1)&&!f.queue.tick(3),"duplicate and skipped ticks rejected");
  require(f.queue.tick(2)&&f.vm.run(10)==odh::VmStatus::returned_true&&f.vm.temps()[2]==99,"Main follows own Idle true branch after queue drained");
 }
 {Fixture f;f.vm.start(key(4096),{1,1,1,0},{3});
  require(f.vm.run(10)==odh::VmStatus::waiting&&f.vm.stack()[0].arguments[0]==2,"empty AllowPush waits after decrement");
  require(f.queue.tick(1)&&f.vm.run(10)==odh::VmStatus::waiting&&f.vm.stack()[0].arguments[0]==1,"AllowPush checks queue each tick");
  require(f.enqueue()&&f.queue.tick(2)&&f.vm.run(10)==odh::VmStatus::yielded,"queue arrival wakes wait and starts action");
  require(f.queue.observe()&&f.queue.tick(3)&&f.vm.run(10)==odh::VmStatus::returned_true,"post-action parent reentry completes countdown");
 }
 {Fixture f(0);require(f.enqueue(),"request before AllowPush0");
  require(f.vm.run(10)==odh::VmStatus::returned_true&&f.queue.size()==1&&f.vm.temps()[0]==0,"AllowPush0 notifies but never dispatches action");
  require(f.world.find(1)->object_words.at(8)==80,"notification bit6 preserves other flag bits");
 }
 {Fixture f(0);f.vm.start(key(4096),{1,1,1,0},{2});
  require(f.vm.run(10)==odh::VmStatus::waiting&&f.vm.stack()[0].arguments[0]==1,"AllowPush0 future deadline");
  require(f.queue.tick(1)&&f.vm.status()==odh::VmStatus::waiting,"future idle not polled prematurely");
  require(f.queue.tick(2)&&f.vm.run(10)==odh::VmStatus::waiting&&f.vm.stack()[0].arguments[0]==0,"zero idleStart sentinel uses decrement one");
  require(f.queue.tick(3)&&f.vm.run(10)==odh::VmStatus::returned_true,"future idle completes on following deadline");
 }
 {Fixture f(0);f.vm.start(key(4096),{1,1,1,0},{8});f.vm.run(10);
  const auto ticket=f.vm.pending()->ticket;
  require(f.enqueue()&&f.queue.tick(1)&&f.vm.status()==odh::VmStatus::waiting&&
      f.vm.pending()->ticket==ticket&&f.vm.stack()[0].arguments[0]==7,
      "ordinary enqueue does not wake source nonpush idle before deadline");
  for(unsigned tick=2;tick<=8;++tick)require(f.queue.tick(tick),"nonpush idle clock reaches source deadline");
  require(f.vm.run(10)==odh::VmStatus::returned_true&&f.queue.size()==1,
      "source deadline reexecutes nonpush idle and publishes queued-action notification");
 }
 {Fixture f;f.vm.start(key(4096),{1,1,1,0},{8});f.vm.run(10);
  require(f.queue.interrupt()&&f.queue.tick(1)&&f.vm.run(10)==odh::VmStatus::returned_true,"explicit Main interrupt wakes countdown");
 }
 {Fixture f;f.enqueue();f.vm.run(10);f.queue.observe();
  require(!f.queue.enqueue(key(4096),{9,2,2,22})&&!f.queue.enqueue(key(4096),{1,9,9,22}),"wrong caller or absent target rejected");
  f.vm.start(key(4096),{1,1,1,0},{0});
  require(f.queue.bind(f.vm)&&f.queue.tick(1)&&f.queue.size()==0&&!f.queue.active()&&!f.queue.completion(),"restart invalidates events and queued state even after rebind");
 }
 {Fixture f;f.vm.start(key(4096),{1,1,1,0},{0});
  require(f.enqueue()&&f.queue.size()==1,"empty interaction queue adopts the first saved-world Main generation");
 }
 {Fixture f;f.enqueue();f.world.remove(2);f.world.create(2,odh::EntityKind::object);
  require(f.vm.run(10)==odh::VmStatus::fault&&f.vm.stack().size()==1&&f.vm.stack()[0].arguments[0]==0,"recycled target generation fails before parent write/action execution");
 }
 {Fixture f;f.enqueue();require(f.vm.run(1)==odh::VmStatus::budget_exhausted&&f.queue.active(),"active action can await next budget slice");
  f.world.remove(2);f.world.create(2,odh::EntityKind::object);
  require(!f.queue.observe()&&!f.queue.tick(1)&&!f.enqueue()&&f.vm.temps()[0]==0,"active target recycle rejected before further host execution");
 }
 {Fixture f;f.world.remove(1);f.world.create(1,odh::EntityKind::avatar);
  require(!f.queue.tick(1)&&!f.enqueue()&&!f.queue.bind(f.vm),"recycled caller invalidates controller");
 }
 {Fixture f(0,2,false);f.enqueue();
  require(f.vm.run(10)==odh::VmStatus::fault&&f.vm.stack()[0].arguments[0]==0,"unknown Flags fails atomically rather than fabricate notification");
 }
 {Fixture f;f.vm.run(10);require(!f.queue.interrupt(),"terminal Main cannot be interrupted");}
 {Fixture f(2);require(f.vm.run(10)==odh::VmStatus::fault,"unknown AllowPush profile rejected");}
 {Fixture f;f.enqueue(9999);require(f.vm.run(10)==odh::VmStatus::fault&&f.vm.stack().size()==1&&f.vm.stack()[0].arguments[0]==0&&f.queue.size()==1,"missing executable remains faulted request without false completion");}
 std::cout<<checks<<" interaction checks passed (synthetic fixtures only)\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
