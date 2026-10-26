#include "odh/interaction.hpp"
#include "odh/entity_thread_controllers_snapshot_codec.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
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
void native_push_interaction_checks() {
    odh::World world;
    world.create(1,odh::EntityKind::avatar);world.create(3,odh::EntityKind::avatar);
    world.create(2,odh::EntityKind::object);world.declare_object_word(2,25,0);
    world.declare_object_word(1,8,16);world.declare_object_word(3,8,16);
    odh::InteractionQueue caller_queue(world,1),recipient_queue(world,3);
    odh::Vm* caller_vm=nullptr;odh::Vm* recipient_vm=nullptr;
    std::optional<odh::Ts1PushInteractionRequest> decoded;
    bool present=true,wrong_owner=false,throw_lookup=false;
    const auto push=odh::ts1_push_interaction_primitive(world,
        [&](const odh::Ts1PushInteractionRequest& request)->std::optional<odh::Ts1PushInteractionAction>{
            decoded=request;
            if(throw_lookup)throw odh::FormatError("fixture malformed resource");
            if(!present)return std::nullopt;
            return odh::Ts1PushInteractionAction{key(4097),wrong_owner?99U:22U,0,key(4098)};
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
            queued.actions.front().check&&queued.actions.front().check->id==4098,
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
                            {7,0,7,0,0,0,0,0},{7,0,0,8,0,0,0,0},{7,0,0,1,9,0,0,0}}){
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
        auto prioritized=push_instruction;prioritized.operands[2]=code;
        require(push(frame,prioritized,caller.temps(),{&caller,static_cast<std::uint64_t>(20U+code)}).disposition==odh::PrimitiveDisposition::goto_true&&
            decoded&&decoded->priority_code==code&&decoded->priority==expected_priorities[code]&&
            decoded->mode==expected_modes[code],"PushInteraction maps every TS1 priority to priority/mode");
    }
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
}
int main(int argc,char** argv){try {
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
 user_permission_checks();
 eligibility_checks();
 dispatch_priority_checks();
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
  require(f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},50,odh::InteractionQueueMode::normal,0,false,false)&&
          f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},30,odh::InteractionQueueMode::normal,0,false,false)&&
          f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},2,odh::InteractionQueueMode::normal,0,false,true),
          "priority and tail actions enter the same live TS1 queue");
  auto state=f.queue.snapshot();
  require(state.actions.size()==4&&state.actions[0].priority==50&&state.actions[1].priority==30&&
          state.actions[2].priority==2&&state.actions[3].priority==1,
          "pending actions order by TS1 priority and PushTail inserts before the first lower priority");
  require(f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},0,odh::InteractionQueueMode::normal,0,true,false),
          "PushHead action enters the TS1 queue");
  state=f.queue.snapshot();
  require(state.actions.front().priority==0&&state.actions.back().priority==1&&
          state.actions.front().flags==(1U<<28|1U<<29),
          "PushHead ignores priority and records skip-permission and queue flags");
 }
 { // Parent modes partition ordinary insertion, and RunImmediately never masquerades as an idle action.
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
  require(!f.queue.enqueue_ts1(key(4097),{1,2,2,22},{},100,odh::InteractionQueueMode::normal,1U<<2,false,false)&&
          f.queue.snapshot().actions.size()==before.actions.size(),
          "RunImmediately stays explicitly unsupported without tick-boundary injection");
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
  require(f.enqueue()&&f.queue.tick(1)&&f.vm.run(10)==odh::VmStatus::returned_true,"queued request wakes nonpush idle notification before deadline");
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
