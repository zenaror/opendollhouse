#include "odh/npc_avatar.hpp"
#include <iostream>

namespace {
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
constexpr std::uint32_t guid=0xfc72c0c0;
constexpr odh::CodeOwner owner=77;
odh::Instruction assign(std::uint8_t scope,std::uint16_t index,std::int16_t value,std::uint8_t next) {
    const auto raw=static_cast<std::uint16_t>(value);
    return {2,next,253,{static_cast<std::uint8_t>(index),static_cast<std::uint8_t>(index>>8),
        static_cast<std::uint8_t>(raw),static_cast<std::uint8_t>(raw>>8),0,5,scope,7}};
}
odh::Bhav routine(std::initializer_list<odh::Instruction> code){return {0x8002,code,{},odh::Bhav::Metadata{0,4,0,0}};}
odh::RoutineSet program(){return {
    {odh::routine_key(4096),routine({assign(8,0,11,1),assign(18,58,9,2),assign(18,35,6,3),
        assign(0,0,55,4),assign(14,0,-10,5),assign(42,0,1234,6),{15,254,253,{}}})},
    {odh::routine_key(4097),routine({{2,1,253,{1,0,0,0,0,5,8,8}},{15,255,253,{}}})},
    {odh::routine_key(4098),routine({assign(8,2,99,254)})}
};}
odh::NeighborEntry neighbor(std::int16_t id,std::int16_t family,std::int16_t age) {
    odh::NeighborRecord r{};r.version=4;r.neighbor_id=id;r.guid=guid;r.person_mode=1;
    r.person_data=std::vector<std::int16_t>(80);(*r.person_data)[61]=family;(*r.person_data)[58]=age;
    (*r.person_data)[65]=1;(*r.person_data)[35]=99;r.name="source-only name";
    r.relationships={{1,999,{100,90}}};return {1,r};
}
odh::NpcAvatarRequestResult resolve(const odh::World& world,std::uint8_t flags=0x14,std::int16_t nid=40) {
    // Same GUID, last serialized record is41, while the explicit request is40.
    odh::NeighborContext neighbors({0,62,{neighbor(40,201,40),neighbor(41,777,6)},0});
    odh::Ts1Temps temps{};temps[0]=23;
    const odh::VmFrame frame{odh::routine_key(4104),0,{1,1,nid,999},{},{}};
    return odh::resolve_npc_avatar_request({0x82e04c5b,5,flags,0,0},frame,temps,neighbors,world);
}
odh::ObjectDefinition definition(){odh::ObjectDefinition d{};
    d.version=138;d.guid=guid;d.object_type=2;d.num_attributes=2;d.level_offset=0;return d;}
odh::EntryPointTable entries(){odh::EntryPointTable e{};e.entries.resize(9);
    e.entries[0].action=4096;e.entries[8].action=4097;e.entries[1].action=4098;return e;}
odh::StringTable strings(const char* name){return {odh::StringTableState::decoded,-3,{{1,name,{}}},0};}
odh::NpcAvatarBodyInputs body(){odh::NpcAvatarBodyInputs b;
    b.body=odh::AvatarBodyStrings{30,0,"adult","body","head",{},"Nora",{"Nora"}};
    b.global_walk=strings("adult-walk");b.global_swim=strings("adult-swim");
    b.birth_time=odh::NpcAvatarBirthTime{2001,9,7,12,34};return b;}
struct Fixture {
    odh::World world{{},odh::AttributePolicy::ts1_bounded};
    odh::NpcAvatarRequest request;
    odh::EntryPointTable table=entries();
    odh::NpcAvatarBodyInputs input=body();
    odh::RandomState random{12345};
    unsigned sync_calls=0;
    Fixture(){
        require(world.create(1,odh::EntityKind::object,{})&&world.define_type(1,0x81e6bef9)&&
            world.set_position(1,{96,128,1,2}),"positioned caller portal fixture");
        auto resolved=resolve(world);
        require(resolved.status==odh::NpcAvatarRequestStatus::ready&&resolved.request.has_value(),"owned NID request resolves");
        request=std::move(*resolved.request);
        require(odh::materialize_npc_avatar(world,2,request,definition(),table),"materialize actual avatar banks");
        require(world.find(2)->object_words.at(25)==0,
            "shared avatar constructor initializes raw lockout count before Thread clock admission");
        odh::VmFrame address;address.context.caller=2;
        require(world.find(2)->object_words.at(1)==0&&world.find(2)->object_words.at(29)==0&&
            world.read(address,3,1)==0&&world.read(address,3,29)==-5,
            "NPC constructor supplies NORTH and zero Room backing before any Init or source placement");
        const auto* fresh=world.find(2);const odh::WorldEntityReference ref{2,fresh->generation};
        require(fresh->object_words.at(14)==-1&&fresh->object_words.at(26)==0&&
            world.native_slot0(ref)==std::optional<odh::WorldEntityReference>{{0,0}}&&
            world.native_position(ref)==std::optional<odh::EntityPosition>{{-16,-16,1,0}}&&
            !fresh->position&&!fresh->attachment,
            "actual fresh NPC constructor owns empty native slot0, unlinked raw parent/slot and OOW scalar level");
    }
    std::unique_ptr<odh::Vm> vm(odh::RoutineSet routines=program()) {
        return std::make_unique<odh::Vm>(odh::RoutineSet{},world.vm_memory(),odh::VmLimits{},odh::VmPrimitive{},
            odh::RoutineOwners{{owner,std::move(routines)}});
    }
    odh::VmPrimitive synchronous(){return [this](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
        require(instruction.opcode==15&&dispatch.check_mode&&frame.context.caller==2&&frame.context.callee==2&&
            frame.context.stack_object==0&&frame.context.code_owner==owner&&frame.exact_arguments&&frame.arguments==std::vector<std::int16_t>(4,0),
            "Init and EP8 use exact synchronous self/self/stack0 explicit owner and four zero arguments");
        ++sync_calls;return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
    };}
};
}
int main(){try{
    {
        odh::World candidate;
        require(odh::materialize_npc_avatar_base(candidate,2,definition(),entries(),std::int16_t{123}),
            "resource-only fresh constructor accepts explicit authenticated catalog value");
        const auto* fresh=candidate.find(2);
        require(fresh&&fresh->object_words.at(41)==123&&fresh->object_words.at(78)==0&&fresh->person_data[7]==0&&fresh->motives[3]==0,
            "fresh complete base has price and final scalar before later body Init changes personality and motives");
        require(odh::materialize_npc_avatar_base(candidate,3,definition(),entries())&&!candidate.find(3)->object_words.count(41),
            "metadata-only constructor never fabricates initial value for restored NPC authority");
        const auto* metadata=candidate.find(3);
        require(!metadata->native_slot0&&!metadata->native_out_of_world_level&&
            metadata->object_words.at(14)==0&&!metadata->object_words.count(26),
            "resource metadata reconstruction cannot fill unknown native hierarchy in older saved World state");
    }
    {
        Fixture f;
        require(f.request.requested_neighbor.guid==guid&&f.request.creation.guid==0x82e04c5b&&
            f.request.requested_neighbor.neighbor_id==40&&f.request.constructor_neighbor&&
            f.request.constructor_neighbor->neighbor_id==41&&f.request.main_param==23&&f.request.main_stack_object==0,
            "UseNeighbor ignores literal GUID and captures explicit NID, GUID alias and MainParam");
        require(f.request.requested_neighbor.relationships.empty()&&f.request.requested_neighbor.name.empty()&&
            f.request.requested_neighbor.person_data->size()==80,"queued request owns bounded inheritance data after provider destruction");
        const auto* a=f.world.find(2);
        require(a&&a->kind==odh::EntityKind::avatar&&a->person_data.size()==101&&a->slots.size()==3&&
            a->motives.size()==16&&a->attributes.size()==2&&a->definition->guid==guid&&!a->position&&
            a->kill_timeout==-1&&a->object_words.at(8)==256,"native constructor is avatar101/slots3/motives16 and base flag profile");
        auto code=program();code[odh::routine_key(4098)].metadata->arguments=9;
        auto vm=f.vm(std::move(code));unsigned attempts=0;
        const odh::EntityPosition retry_position{144,160,1,f.request.position.direction};
        const auto placement=[&](odh::World& world,std::int16_t id,const auto& pos,std::int16_t caller,bool ignore){
            ++attempts;const auto* actor=world.find(id);
            require(id==2&&caller==1&&pos==(attempts==1?f.request.position:retry_position)&&vm->status()==odh::VmStatus::running&&
                vm->steps()==0&&vm->stack().back().pc==0&&vm->stack().back().exact_arguments&&
                vm->stack().back().arguments==std::vector<std::int16_t>({23,0,0,0})&&
                vm->temps()[0]==11&&vm->temps()[1]==11&&vm->temps()[2]==0&&vm->temp_xl()[0]==1234,
                "Main is prepared, not dispatched, and Init/EP8 registers survive Main start");
            if(attempts==1){
                require(!ignore&&actor->person_data.size()==101&&actor->person_data[58]==30&&
                    actor->person_data[31]==0&&actor->motives[0]==100&&actor->motives[11]==0&&
                    actor->person_data[7]==1000&&actor->object_words.at(45)==0,
                    "first default placement follows body/motives but precedes NBRS and birth");
                require(world.set_position(1,{144,160,1,6}),"first placement effects move caller before source retry");
                return odh::NpcPositionAttemptStatus::blocked;
            }
            require(ignore&&actor->person_data.size()==88&&actor->person_data[31]==41&&
                actor->person_data[61]==777&&actor->person_data[32]==1&&actor->person_data[58]==6&&
                actor->object_words.at(45)==2001&&actor->object_words.at(21)==34,
                "retry observes GUID-selected88-word inheritance and birth before explicit requested-NID inheritance");
            require(world.set_position(id,pos),"placement callback performs actual position publication");
            return odh::NpcPositionAttemptStatus::placed;
        };
        const auto before_seed=f.random.seed();
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,201,placement,100,f.synchronous(),true);
        require(result.completed&&result.main_started&&!result.placement_blocked&&result.position_attempts==2&&
            result.inheritance.size()==2&&result.inheritance[0].neighbor_id==41&&result.inheritance[1].neighbor_id==40&&
            result.body&&result.body->selected_name=="Nora"&&f.sync_calls==2&&f.random.seed()!=before_seed,
            "source-order constructor completes with owned appearance and two distinct inheritance events");
        a=f.world.find(2);
        require(a->person_data.size()==88&&a->person_data[31]==40&&a->person_data[61]==201&&a->person_data[32]==1&&
            a->person_data[65]==0&&a->person_data[35]==6&&a->person_data[58]==40&&a->position==retry_position&&
            a->attributes[0]==55,"final requested neighbor preserves constructor-selected PersonType, gender and schedule without FAMI overwrite");
        require(vm->run(10)==odh::VmStatus::returned_true&&vm->temps()[2]==99,"only explicit later host dispatch executes Main");
    }
    {
        Fixture f;auto vm=f.vm();unsigned attempts=0;
        const auto placement=[&](auto& world,auto id,const auto& pos,auto,bool ignore){
            ++attempts;require(!ignore&&world.find(id)->person_data.size()==101,"successful first attempt does not pre-inherit or ignore group");
            require(world.set_position(id,pos),"normal first placement");return odh::NpcPositionAttemptStatus::placed;
        };
        require(f.world.set_position(1,{112,128,1,4}),"caller can move after request capture");
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},placement,100,f.synchronous(),true);
        require(result.completed&&attempts==1&&f.world.find(2)->position==f.request.position,
            "queued placement uses captured caller coordinates instead of delayed current coordinates");
    }
    {
        Fixture f;auto vm=f.vm();unsigned attempts=0;
        const auto blocked=[&](auto&,auto,const auto&,auto,bool ignore){++attempts;
            require(ignore==(attempts==2),"Underneath retry only after first blocked placement");return odh::NpcPositionAttemptStatus::blocked;};
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},blocked,100,f.synchronous(),true);
        require(!result.completed&&result.placement_blocked&&attempts==2&&result.inheritance.size()==1&&
            f.world.find(2)->person_data[31]==41&&!f.world.find(2)->position,
            "genuine placement failure stops before final UseNeighbor inheritance; private candidate awaits host disposal");
    }
    {
        Fixture f;auto vm=f.vm();unsigned attempts=0;
        const auto unsupported=[&](auto&,auto,const auto&,auto,bool){++attempts;return odh::NpcPositionAttemptStatus::unsupported;};
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},unsupported,100,f.synchronous(),true);
        require(!result.completed&&!result.placement_blocked&&attempts==1&&result.inheritance.empty(),
            "unsupported placement cannot be converted into a game-blocked retry or success");
    }
    {
        Fixture f;auto code=program();code[odh::routine_key(4098)].metadata->arguments=9;
        auto vm=f.vm(std::move(code));f.request.main_param=0;
        vm->temps()[0]=77;vm->temp_xl()[0]=45678;
        const auto placed=[&](auto& world,auto id,const auto& position,auto,bool){
            require(vm->steps()==0&&vm->stack().back().arguments==std::vector<std::int16_t>(4,0)&&vm->stack().back().exact_arguments&&
                vm->temps()[0]==77&&vm->temps()[1]==0&&vm->temp_xl()[0]==45678,
                "outside scheduler startup clones registers and null Main arguments normalize to four zeros");
            require(world.set_position(id,position),"place clone-mode NPC after real source-sync startup");
            return odh::NpcPositionAttemptStatus::placed;
        };
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},placed,100,f.synchronous(),false);
        require(result.completed&&f.world.find(2)->attributes[0]==55&&f.sync_calls==2,
            "outside-tick register cloning still preserves actual Init World effects");
    }
    for(int bad_phase:{-1,0,8}){
        Fixture f;auto code=program();
        auto defaults=routine({});defaults.metadata->arguments=0;
        for(std::uint16_t n=0;n<4;++n)defaults.instructions.push_back({2,
            static_cast<std::uint8_t>(n==3?254:n+1),255,
            {static_cast<std::uint8_t>(n),0,0,0,0,2,9,7}});
        auto outside=defaults;outside.metadata->arguments=9;
        outside.instructions={{2,254,255,{4,0,0,0,0,2,9,7}}};
        code[odh::routine_key(4096)]=bad_phase==0?outside:defaults;
        code[odh::routine_key(4097)]=bad_phase==8?outside:defaults;
        auto vm=f.vm(std::move(code));unsigned attempts=0;
        const auto placed=[&](auto& world,auto id,const auto& position,auto,bool){++attempts;
            require(world.set_position(id,position),"place NPC after default-argument constructor entries");
            return odh::NpcPositionAttemptStatus::placed;};
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},placed,100);
        if(bad_phase<0)require(result.completed&&result.steps==8&&attempts==1,
            "NPC Init and EP8 can both read default Params0..3 with zero metadata arguments");
        else require(!result.completed&&!result.main_started&&attempts==0&&
            result.stages.back().status==odh::VmStatus::fault,
            "NPC constructor Params stop at four words even when either entry declares nine");
    }
    {
        Fixture f;auto code=program();code[odh::routine_key(4096)]=routine({{18,254,255,{}}});
        auto vm=f.vm(std::move(code));bool deleted=false;unsigned attempts=0;const auto seed=f.random.seed();
        const auto placed=[&](auto&,auto,const auto&,auto,bool){++attempts;return odh::NpcPositionAttemptStatus::placed;};
        const auto remove=[&](const auto&,const auto&,const auto&,const auto&){deleted=true;
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::lifecycle_deleted,{}};};
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},placed,
            100,remove,false,[&]{return deleted;});
        require(result.completed&&result.self_deleted&&!result.main_started&&result.stages.size()==1&&
            result.stages[0].status==odh::VmStatus::ready&&attempts==0&&f.random.seed()==seed&&vm->stack().empty(),
            "typed NPC self-deletion stops before EP8/body/RNG/placement/Main with an explicit identity proof");
    }
    for(bool wait:{false,true}){
        Fixture f;auto code=program();
        if(wait)code[odh::routine_key(4096)]=routine({{0,254,253,{}}});
        auto vm=f.vm(std::move(code));const auto seed=f.random.seed();unsigned attempts=0;
        const auto position=[&](auto&,auto,const auto&,auto,bool){++attempts;return odh::NpcPositionAttemptStatus::blocked;};
        const auto primitive=[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};};
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},position,
            wait?100:1,primitive);
        require(!result.completed&&!result.main_started&&attempts==0&&f.random.seed()==seed&&
            f.world.find(2)->person_data.size()==101,"async/budget startup failure never runs body, placement, inheritance or Main");
    }
    {
        Fixture f;auto vm=f.vm();f.input.birth_time.reset();const auto seed=f.random.seed();
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},
            [](auto&,auto,const auto&,auto,bool){return odh::NpcPositionAttemptStatus::blocked;});
        require(!result.completed&&vm->status()==odh::VmStatus::ready&&vm->run_generation()==0&&f.random.seed()==seed,
            "missing clock provenance fails preflight without starting VM or inventing birth time");
    }
    {
        Fixture f;auto vm=f.vm();vm->start(odh::routine_key(4098),{2,2,0,owner});
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,{},
            [](auto&,auto,const auto&,auto,bool){return odh::NpcPositionAttemptStatus::blocked;});
        require(!result.completed&&vm->stack().back().key.id==4098&&vm->stack().back().pc==0,
            "startup does not replace an already active entity continuation");
    }
    {
        odh::World world;require(world.create(1,odh::EntityKind::object,{})&&world.set_position(1,{16,16,1,0}),"request-only World");
        require(resolve(world,0x14,99).status==odh::NpcAvatarRequestStatus::missing_neighbor,
            "missing requested NID is a genuine missing-neighbor result");
        require(resolve(world,4).request->main_param==0,"without PassTemp0 source MainParam is zero");
        require(resolve(world,0).status==odh::NpcAvatarRequestStatus::unsupported&&
            resolve(world,0x44).status==odh::NpcAvatarRequestStatus::unsupported,
            "non-neighbor and callback flag combinations reject before materialization");
        auto request=*resolve(world).request;auto d=definition();d.object_type=4;
        const auto generation=world.snapshot().generation_sequence;
        require(!odh::materialize_npc_avatar(world,2,request,d,entries())&&!world.find(2)&&
            world.snapshot().generation_sequence==generation,"ordinary object OBJD cannot become an NPC avatar");
        d=definition();d.num_attributes.reset();
        require(!odh::materialize_npc_avatar(world,2,request,d,entries()),"incomplete resource constructor metadata rejects");
        request.caller_generation++;
        require(!odh::materialize_npc_avatar(world,2,request,definition(),entries()),"stale caller generation cannot authorize construction");
    }
    {
        odh::World world;
        require(odh::materialize_npc_avatar_base(world,9,definition(),entries()),
            "restore metadata base needs no fabricated caller or NPC request");
        const auto* actor=world.find(9);
        require(actor&&actor->kind==odh::EntityKind::avatar&&actor->person_data.size()==101&&
            actor->person_data[31]==0&&actor->slots.size()==3&&actor->motives.size()==16&&
            !actor->position&&!actor->attachment&&actor->definition->guid==guid,
            "resource-only base preserves fresh typed constructor graph without inheritance or placement");
    }
    {
        Fixture f;
        require(f.world.place_out_of_world(1),"travel controller can itself be OOW");
        odh::NeighborContext neighbors({0,62,{neighbor(40,201,40),neighbor(41,777,6)},0});
        odh::VmFrame frame{odh::routine_key(4104),0,{1,1,0,999},{},{}};
        auto resolved=odh::resolve_npc_avatar_out_of_world_request(40,frame,neighbors,f.world);
        require(resolved.status==odh::NpcAvatarRequestStatus::ready&&resolved.request&&
            resolved.request->requested_neighbor.neighbor_id==41&&resolved.request->creation.guid==guid,
            "travel token NID selects GUID then constructor uses GUID-selected neighbor");
        f.request=*resolved.request;auto thread=f.vm();
        const auto result=odh::initialize_npc_avatar(f.world,*thread,2,owner,f.request,f.table,f.input,f.random,
            201,{},10000,f.synchronous());
        require(result.completed&&result.main_started&&result.position_attempts==0&&
            !f.world.find(2)->position&&!f.world.find(2)->attachment&&f.world.find(2)->person_data[31]==41&&
            result.inheritance.size()==1,"shared OOW constructor runs exact Init/body/inheritance/Main without UnderneathMe placement");
        require(odh::resolve_npc_avatar_out_of_world_request(-99,frame,neighbors,f.world).status==
            odh::NpcAvatarRequestStatus::missing_neighbor,"missing signed companion NID is explicit");
    }
    {
        // Literal Person resources need not have a neighborhood row. The same
        // Init/EP8 path must therefore see real motive/person banks before Init.
        Fixture f;odh::NeighborContext empty(odh::Nbrs{});
        odh::VmFrame frame;frame.context={1,1,0,999};odh::Ts1Temps temps{};temps[0]=73;
        odh::CreateObjectInstanceRequest creation{guid,6,0x30,0,0};
        auto direct=odh::resolve_npc_avatar_guid_request(creation,frame,temps,empty,f.world);
        require(direct.status==odh::NpcAvatarRequestStatus::ready&&direct.request&&
            direct.request->guid()==guid&&!direct.request->constructor_neighbor&&
            direct.request->requested_neighbor.guid==0&&direct.request->main_param==73,
            "literal Person GUID without NBRS preserves absence and captured MainParam, FaceStack ignored by pin");
        f.request=*direct.request;auto thread=f.vm();
        const auto result=odh::initialize_npc_avatar(f.world,*thread,2,owner,f.request,f.table,f.input,f.random,
            201,{},10000,f.synchronous());
        require(result.completed&&result.main_started&&result.inheritance.empty()&&
            f.world.find(2)->kind==odh::EntityKind::avatar&&f.world.find(2)->motives.size()==16&&
            f.world.find(2)->person_data.size()==101&&f.world.find(2)->person_data[31]==0&&
            thread->stack().back().arguments.front()==73,
            "direct resource runs real avatar Init/body/Main while preserving NeighborID zero without invented inheritance");
        creation.flags=0x80;
        require(odh::resolve_npc_avatar_guid_request(creation,frame,temps,empty,f.world).status==
            odh::NpcAvatarRequestStatus::unsupported,"literal constructor cannot bypass PersistID authority");
    }
    {
        Fixture f;odh::NeighborContext empty(odh::Nbrs{});
        require(f.world.place_out_of_world(1),"global creator may be OOW");
        require(f.world.create(3,odh::EntityKind::object,{})&&f.world.define_type(3,123)&&
            f.world.set_position(3,{144,192,1,6}),"local marker owns actual destination pose");
        odh::VmFrame frame;frame.context={1,1,0,999};frame.locals={3};
        odh::CreateObjectInstanceRequest creation{guid,8,0x20,0,0};
        auto resolved=odh::resolve_npc_avatar_guid_request(creation,frame,{},empty,f.world);
        require(resolved.request&&resolved.request->direct_placement&&
            resolved.request->direct_placement->reference->id==3&&resolved.request->position==*f.world.find(3)->position,
            "BelowObjectInLocal captures referenced marker without requiring creator position");
        f.request=*resolved.request;auto vm=f.vm();unsigned attempts=0;
        const auto placement=[&](odh::World& w,std::int16_t id,const odh::EntityPosition& pos,std::int16_t,bool ignore){
            ++attempts;require(!ignore&&pos==odh::EntityPosition{144,192,1,6},"local marker frozen pose is used once");
            require(w.set_position(id,pos),"publish actual captured default placement");return odh::NpcPositionAttemptStatus::placed;};
        const auto initialized=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,
            201,placement,10000,f.synchronous());
        require(initialized.completed&&attempts==1&&f.world.find(2)->position==std::optional<odh::EntityPosition>{{144,192,1,6}},
            "literal resource actor reaches referenced marker from OOW creator");
    }
    {
        Fixture f;odh::NeighborContext empty(odh::Nbrs{});
        require(f.world.place_out_of_world(1,6),"OOW caller retains authentic direction");
        odh::VmFrame frame;frame.context={1,1,0,999};frame.locals={1};
        auto resolved=odh::resolve_npc_avatar_guid_request({guid,8,0x20,0,0},frame,{},empty,f.world);
        require(resolved.request&&resolved.request->direct_placement&&!resolved.request->direct_placement->destination,
            "OOW local anchor remains absent destination, never fabricated map coordinates");
        f.request=*resolved.request;auto vm=f.vm();
        const auto initialized=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,
            201,{},10000,f.synchronous());
        require(!initialized.completed&&initialized.placement_blocked&&initialized.position_attempts==0&&!f.world.find(2)->position&&
            f.world.find(2)->object_words.at(1)==6,"BelowLocal OOW sentinel preserves constructor effects/direction and requires caller deletion/false");
    }
    {
        Fixture f;odh::NeighborContext empty(odh::Nbrs{});
        odh::VmFrame frame;frame.context={1,1,0,999};frame.locals={99};
        require(odh::resolve_npc_avatar_guid_request({guid,8,0x20,0,0},frame,{},empty,f.world).status==
            odh::NpcAvatarRequestStatus::unsupported,"missing local marker cannot fall back to creator pose");
        frame.locals={1};auto resolved=odh::resolve_npc_avatar_guid_request({guid,8,0x20,0,0},frame,{},empty,f.world);
        require(resolved.request.has_value(),"valid local marker request resolves");f.request=*resolved.request;auto vm=f.vm();unsigned attempts=0;
        const auto initialized=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,201,
            [&](auto&,auto,const auto&,auto,bool ignore){++attempts;require(!ignore,"BelowLocal does not use Underneath caller retry");
                return odh::NpcPositionAttemptStatus::blocked;},10000,f.synchronous());
        require(!initialized.completed&&initialized.placement_blocked&&attempts==1&&!f.world.find(2)->position,
            "non-Underneath blocked placement requests caller deletion/false without an Underneath retry");
    }
    {
        Fixture f;odh::NeighborContext empty(odh::Nbrs{});
        require(f.world.place_out_of_world(1,6),"Underneath creator starts OOW facing west");
        odh::VmFrame frame;frame.context={1,1,0,999};
        auto resolved=odh::resolve_npc_avatar_guid_request({guid,5,0x20,0,0},frame,{},empty,f.world);
        require(resolved.request&&resolved.request->position.direction==6&&
            !resolved.request->direct_placement->destination,"OOW plan preserves captured direction independently of absent coordinates");
        f.request=*resolved.request;auto vm=f.vm();auto init=f.synchronous();unsigned attempts=0;
        const auto primitive=[&](const auto& fr,const auto& ins,const auto& temps,const auto& dispatch){
            const auto result=init(fr,ins,temps,dispatch);
            if(f.sync_calls==1)require(f.world.set_position(1,{208,256,1,2}),"Init moves creator to real map location and new heading");
            return result;};
        const auto placement=[&](odh::World& w,std::int16_t id,const odh::EntityPosition& pos,std::int16_t,bool ignore){
            ++attempts;require(ignore&&pos==odh::EntityPosition{208,256,1,6},
                "Underneath retry uses post-Init Caller coordinates and pre-Init direction");
            require(w.set_position(id,pos),"publish actual retry placement");return odh::NpcPositionAttemptStatus::placed;};
        const auto initialized=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,
            201,placement,10000,primitive);
        require(initialized.completed&&!initialized.placement_blocked&&attempts==1&&
            f.world.find(2)->position==std::optional<odh::EntityPosition>{{208,256,1,6}},
            "OOW initial placement is not mistaken for literal OOW operand and still performs source Underneath retry");
    }
    {
        Fixture f;odh::NeighborContext empty(odh::Nbrs{});
        require(f.world.create(3,odh::EntityKind::object,{})&&f.world.define_type(3,123)&&
            f.world.set_position(3,{144,192,1,6}),"flag matrix has real stack/parameter/local marker");
        require(f.world.create(40,odh::EntityKind::object,{})&&f.world.define_type(40,456)&&
            f.world.set_position(40,{176,208,1,4}),"NeighborID can also identify a real source StackObject");
        odh::NeighborContext neighbors({0,62,{neighbor(40,201,40),neighbor(41,777,6)},0});
        require(f.world.declare_object_word(1,1,2),"mode9 reads the Caller Direction property, not merely its geometric pose");
        odh::VmFrame frame;frame.context={1,1,3,999};frame.arguments={3};frame.locals={3,0};
        odh::Ts1Temps temps{};temps[0]=-123;
        const auto generation=f.world.snapshot().generation_sequence;const auto seed=f.random.seed();
        for(bool use_neighbor:{false,true})for(unsigned position=0;position!=10;++position)for(unsigned flags=0;flags!=256;++flags){
            frame.context.stack_object=use_neighbor?40:3;
            const odh::CreateObjectInstanceRequest creation{guid,static_cast<std::uint8_t>(position),
                static_cast<std::uint8_t>(flags),static_cast<std::uint8_t>(position==9?1:0),0};
            const auto resolved=use_neighbor?odh::resolve_npc_avatar_request(creation,frame,temps,neighbors,f.world):
                odh::resolve_npc_avatar_guid_request(creation,frame,temps,empty,f.world);
            const bool supported=position!=2&&position!=4&&
                (use_neighbor?((flags&4)!=0&&(flags&~0x3fU)==0):(flags&~0x3bU)==0);
            if((resolved.status==odh::NpcAvatarRequestStatus::ready)!=supported||resolved.request.has_value()!=supported)
                throw std::runtime_error("avatar flag/position matrix kind="+std::string(use_neighbor?"neighbor":"literal")+
                    " flags="+std::to_string(flags)+" position="+std::to_string(position)+
                    " status="+std::to_string(static_cast<unsigned>(resolved.status))+
                    " expected_ready="+std::to_string(supported)+" diagnostic="+resolved.diagnostic);
            require(true,"all literal and neighbor flag/position combinations obey finite non-container source profile");
            if(supported){
                require(resolved.request->main_param==((flags&16)?-123:((flags&2)?1:0))&&
                    resolved.request->main_stack_object==((flags&2)?(use_neighbor?40:3):0),
                    "PassTemp0 overrides PassObjectIDs MainParam while MainStackOBJ remains the captured source ID");
            }
        }
        require(f.world.snapshot().generation_sequence==generation&&f.world.snapshot().entities.size()==4&&
            f.random.seed()==seed&&temps[0]==-123,"whole admission matrix allocates no ID, runs no Init, and consumes no RNG or TEMP");
        frame.context.stack_object=99;
        const auto no_stack=odh::resolve_npc_avatar_guid_request({guid,6,2,0,0},frame,temps,empty,f.world);
        require(no_stack.request&&no_stack.request->main_stack_object==0&&no_stack.request->main_param==1,
            "PassObjectIDs with missing stack pointer passes null StackObject and real Caller argument");
    }
    {
        Fixture f;odh::NeighborContext empty(odh::Nbrs{});
        odh::VmFrame frame;frame.context={1,1,0,999};
        const odh::CreateObjectInstanceRequest creation{guid,5,0x21,0,0};
        const auto first=odh::resolve_npc_avatar_guid_request(creation,frame,{},empty,f.world);
        const auto second=odh::resolve_npc_avatar_guid_request(creation,frame,{},empty,f.world);
        require(first.request&&second.request,"two queued source requests initially pass NoDuplicate before publication");
        require(odh::materialize_npc_avatar(f.world,3,*first.request,definition(),f.table)&&
            f.world.set_position(3,{97,129,1,0}),"first queued literal actor publishes same destination tile, different subtile");
        const auto generation=f.world.snapshot().generation_sequence;const auto seed=f.random.seed();
        std::string diagnostic;
        require(odh::preflight_npc_avatar_creation(*second.request,f.world,&diagnostic)==odh::NpcAvatarRequestStatus::duplicate&&
            !diagnostic.empty()&&!odh::materialize_npc_avatar(f.world,4,*second.request,definition(),f.table)&&
            !f.world.find(4)&&f.world.snapshot().generation_sequence==generation&&f.random.seed()==seed,
            "final queued recheck returns known source false before allocating ID/Init/RNG, materializer also defends it");
        require(odh::resolve_npc_avatar_guid_request(creation,frame,{},empty,f.world).status==
            odh::NpcAvatarRequestStatus::duplicate,"literal resolver exposes duplicate independently from unsupported");
        require(odh::resolve_npc_avatar_guid_request({guid,5,0x20,0,0},frame,{},empty,f.world).request.has_value(),
            "without NoDuplicate template identity alone does not authorize source false");
        require(f.world.set_position(3,{97,129,2,0})&&
            odh::preflight_npc_avatar_creation(*second.request,f.world)==odh::NpcAvatarRequestStatus::ready,
            "same GUID on another floor is not a source duplicate");
        require(f.world.set_position(3,{112,129,1,0})&&
            odh::preflight_npc_avatar_creation(*second.request,f.world)==odh::NpcAvatarRequestStatus::ready,
            "same GUID on another tile is not a source duplicate");
        require(f.world.create(4,odh::EntityKind::object,{})&&f.world.set_position(4,{97,129,1,0})&&
            odh::preflight_npc_avatar_creation(*second.request,f.world)==odh::NpcAvatarRequestStatus::unsupported,
            "untyped destination occupant is unknown admission, not known duplicate or empty tile");
        require(f.world.place_out_of_world(4)&&f.world.place_out_of_world(1),"remove registered destination buckets");
        const auto oow=odh::resolve_npc_avatar_guid_request(creation,frame,{},empty,f.world);
        require(oow.request&&!oow.request->direct_placement->destination,
            "NoDuplicate preserves known OOW sentinel instead of treating all OOW template instances as duplicates");
    }
    {
        Fixture f;odh::NeighborContext neighbors({0,62,{neighbor(40,201,40),neighbor(41,777,6)},0});
        odh::VmFrame frame;frame.context={1,1,40,999};
        require(f.world.set_position(2,{97,129,1,0}),"existing avatar has resolved NBRS template on caller destination tile");
        const auto result=odh::resolve_npc_avatar_request({0x82e04c5b,5,5,0,0},frame,{},neighbors,f.world);
        require(result.status==odh::NpcAvatarRequestStatus::duplicate&&!result.request,
            "UseNeighbor NoDuplicate substitutes GUID from NBRS before spatial check, not the literal operand GUID");
        require(odh::resolve_npc_avatar_request({0x82e04c5b,5,4,0,0},frame,{},neighbors,f.world).request.has_value(),
            "UseNeighbor without NoDuplicate retains separate host identity/save admission");
    }
    for(bool use_neighbor:{false,true})for(unsigned retirement=0;retirement!=3;++retirement){
        Fixture f;odh::NeighborContext empty(odh::Nbrs{});
        const std::int16_t marker=use_neighbor?40:3;
        require(f.world.create(marker,odh::EntityKind::object,{})&&f.world.define_type(marker,123),"PassIDs stack marker exists at capture");
        const auto old_generation=f.world.find(marker)->generation;
        odh::NeighborContext neighbors({0,62,{neighbor(40,201,40),neighbor(41,777,6)},0});
        odh::VmFrame frame;frame.context={1,1,marker,999};odh::Ts1Temps temps{};temps[0]=-73;
        const odh::CreateObjectInstanceRequest creation{use_neighbor?0x82e04c5b:guid,6,
            static_cast<std::uint8_t>((retirement==2?0x1a:2)|(use_neighbor?4:0)),0,0};
        const auto resolved=use_neighbor?odh::resolve_npc_avatar_request(creation,frame,temps,neighbors,f.world):
            odh::resolve_npc_avatar_guid_request(creation,frame,temps,empty,f.world);
        require(resolved.request&&resolved.request->main_stack_object==marker,"literal and neighbor capture raw MainStackOBJ before Init");
        f.request=*resolved.request;auto vm=f.vm();auto synchronous=f.synchronous();
        const auto primitive=[&](const auto& fr,const auto& ins,const auto& current_temps,const auto& dispatch){
            auto result=synchronous(fr,ins,current_temps,dispatch);
            if(f.sync_calls==2&&retirement){
                require(f.world.remove(marker),"EP8 deletes captured MainStackOBJ before Main lookup");
                if(retirement==2)require(f.world.create(marker,odh::EntityKind::object,{})&&f.world.define_type(marker,456)&&
                    f.world.find(marker)->generation!=old_generation,"EP8 reuses source numeric ID for a different current object");
            }
            return result;
        };
        const auto result=odh::initialize_npc_avatar(f.world,*vm,2,owner,f.request,f.table,f.input,f.random,
            201,{},10000,primitive);
        require(result.completed&&result.main_started&&vm->stack().back().context.stack_object==(retirement==1?0:marker)&&
            vm->stack().back().arguments==std::vector<std::int16_t>({static_cast<std::int16_t>(retirement==2?-73:1),0,0,0}),
            "Main uses post-Init/EP8 indexed StackObject lookup and captured constructor parameter precedence");
        if(use_neighbor)require(result.inheritance.size()==2&&result.inheritance.back().neighbor_id==40&&
            f.world.find(2)->person_data[31]==40,
            "UseNeighbor OOW still applies requested inheritance after GUID-selected constructor inheritance");
        if(retirement==2)require(vm->stack().back().context.stack_object_generation==f.world.find(marker)->generation,
            "Main binds newly indexed generation after EP8 rather than the retired pre-Init identity");
    }
    std::cout<<checks<<" NPC avatar construction checks passed (private native candidates)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
