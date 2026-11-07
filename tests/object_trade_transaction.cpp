// SPDX-License-Identifier: MPL-2.0
#include "odh/object_trade_transaction.hpp"
#include "odh/object_value_registry.hpp"
#include "odh/dynamic_object_groups.hpp"
#include "odh/seated_session_continuation.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
namespace {
using namespace odh;
void require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
void prepared(const ObjectTradeTransactionResult& r,ObjectTradeTransactionStatus status,const char* label){
    if(r.status!=status)throw std::runtime_error(std::string(label)+": actual="+std::to_string(static_cast<int>(r.status))+
        " expected="+std::to_string(static_cast<int>(status))+" decision="+std::to_string(static_cast<int>(r.decision.status))+
        " reason="+std::to_string(static_cast<int>(r.decision.reason))+" diagnostic="+r.diagnostic);
    require(r.candidate.has_value()&&!r.archive.empty(),"prepared transaction has canonical candidate");
    require(encode_session_bundle(decode_session_bundle(r.archive))==r.archive,"prepared archive reopens exactly");
}
SessionBundle source(bool with_clock=true){
    World world;require(world.create(1,EntityKind::avatar)&&world.define_type(1,1)&&world.declare_object_word(1,0,0),"resident fixture");
    if(with_clock)require(world.set_lockout_count_clock(0),"explicit source scheduler clock");
    Vm vm(RoutineSet{});EntityThreadControllers controllers(world,1);require(controllers.bind(vm),"resident controllers");
    EntityVmScheduler scheduler(4);require(scheduler.add(1,vm,[](auto){return true;},[]{return true;},[]{return true;}),"resident scheduler");
    SessionBundle s;s.resources.resource_fingerprint.fill(1);s.resources.language_code=1;
    s.host.neighborhood_mode=1;s.host.house_number=7;
    s.host.runtime_avatar_id=1;s.host.resident_guid=1;s.host.resident_neighbor_id=1;s.host.family_budget=1000;s.host.random_seed=123;
    s.world=world.snapshot();RelationshipState relations;s.relationships=relations.snapshot();
    s.runtime_entities={{1,vm.snapshot(),controllers.snapshot()}};s.scheduler=scheduler.snapshot();
    s.generic_execution=true;s.object_history=SessionObjectHistory{true,{},{}};
    return s;
}
ObjectTradeResource resource(){
    ObjectTradeResource r;r.origin.guid=0x1234;r.origin.source="GameData/Objects/Objects.far";
    r.origin.asset="chair.iff";r.origin.definition_chunk_id=128;
    r.runtime.definition={};r.runtime.definition.guid=r.origin.guid;r.runtime.definition.build_mode_type=0;r.runtime.definition.level_offset=0;
    r.runtime.construction={ObjectConstructionStatus::ready,{0,0}};
    r.runtime.entry_points={};r.runtime.entry_points.entries.resize(33);
    r.runtime.entry_points.entries[8].action=4099;
    r.runtime.entry_points.entries[0].action=4096;r.runtime.entry_points.entries[11].action=4097;r.runtime.entry_points.entries[12].action=4098;
    for(std::uint16_t i=0;i<4;++i)r.runtime.private_routines.emplace(routine_key(static_cast<std::uint16_t>(4096+i)),
        Bhav{0x8002,{{static_cast<std::uint16_t>(99+i),254,255,{}}},{},Bhav::Metadata{0,6,0,0}});
    return r;
}
struct Adapter {
    bool authorized=true,blocked=false,technical_failure=false,ep11_recovers=false,test_globals=false,no_family=false;
    bool saw_globals=false,saw_clock=false,fail_detach=false,require_dead_cleanup=false;unsigned fail_unregister=0,unregister_calls=0,pre_phases=0;std::optional<std::int16_t> expected_clock;
    std::optional<RoutineKey> expected_main;
    std::int16_t sale_temp=0;std::int32_t sale_xl=0;
    std::optional<std::int16_t> init_current_value;
    VmSavedRoutinePolicy saved_policy=VmSavedRoutinePolicy::require_present;
    std::optional<ObjectTradeSelectedPrograms> selected_programs;
    std::optional<VmSnapshot> expected_bound;
    bool lifecycle_source_registers=false;
    std::optional<std::int16_t> ep11_current_value;
    int binds=0;std::vector<std::string> events;std::vector<std::int32_t> observed_budget;
    ObjectTradeBindingFactory factory(){return [this](World& w,RelationshipState&,SessionHostState& host,RandomState& random,RuntimeGlobals* globals,const Ts1Clock* clock,const ObjectTradeResource& resource){
        ++binds;saw_globals=globals!=nullptr;saw_clock=clock!=nullptr;
        if(clock)expected_clock=clock->global(0);
        ObjectTradeRuntimeBinding b;b.memory=w.vm_memory();b.selected_saved_routine_policy=saved_policy;
        b.selected_programs=selected_programs;
        auto actual_thread=std::make_shared<Vm*>(nullptr);
        const auto world_read=b.memory.read;const auto world_write=b.memory.write;
        b.memory.read=[world_read,globals,clock](const VmFrame& frame,std::uint8_t scope,std::int16_t index){
            return scope==6?(globals?globals->read(index,clock):std::nullopt):world_read(frame,scope,index);
        };
        b.memory.write=[world_write,globals](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value){
            return scope==6?(globals&&globals->write(index,value)):world_write(frame,scope,index,value);
        };
        b.command_still_matches=[this]{return authorized;};
        b.primitive_factory=[this,&w,&host,&random,actual_thread](const VmMemory& memory){
            // Retaining this exact memory reference exercises the stable-binding
            // contract. Moving it into Vm would empty these callbacks.
            return [this,&w,&host,&random,&memory,actual_thread](const VmFrame& frame,const Instruction& i,const Ts1Temps& temps,const VmDispatch& dispatch)->PrimitiveOutcome {
                require(memory.read&&memory.write,"candidate memory remains alive and populated");
                VmFrame resident;resident.context.caller=1;
                require(memory.read(resident,3,0).has_value(),"factory-held memory reads actual candidate resident");
                require(frame.arguments==std::vector<std::int16_t>({0,0,0,0}),"source lifecycle frame has exactly four zero args despite metadata6");
                if(i.opcode==101||lifecycle_source_registers)require(temps[7]==sale_temp&&dispatch.origin->temp_xl()[0]==sale_xl,"EP12 clones actual saved Thread Temp/XL");
                else require(temps[7]==0&&dispatch.origin->temp_xl()[0]==0,"external Init/EP8/EP11 each clones original Thread registers");
                if(no_family)require(!host.family_budget,"actorless lifecycle never invents a family budget");
                else if(i.opcode!=102)observed_budget.push_back(*host.family_budget);
                if(test_globals){
                    require(memory.read(frame,6,0)==expected_clock,"candidate clock overlays global shadow");
                    require(memory.write(frame,6,0,999)&&memory.read(frame,6,0)==expected_clock,"global clock shadow write never advances/replaces source clock");
                    require(memory.write(frame,6,3,0),"source global selection write");
                    const auto value=memory.read(frame,6,9);require(value.has_value(),"candidate writable global known");
                    if(i.opcode!=102)require(memory.write(frame,6,9,static_cast<std::int16_t>(*value+1)),"Init/placement/deletion candidate global effects");
                }
                if(expected_main&&i.opcode==99)require(*actual_thread&&(*actual_thread)->stack().empty(),"Init runs before Main is pushed");
                if(expected_main&&i.opcode==100){
                    require(*actual_thread&&(*actual_thread)->status()==VmStatus::running&&(*actual_thread)->steps()==0&&
                        (*actual_thread)->stack().size()==1&&(*actual_thread)->stack().front().pc==0&&
                        (*actual_thread)->stack().front().key.scope==expected_main->scope&&
                        (*actual_thread)->stack().front().key.id==expected_main->id,
                        "EP11 sees source Main already pushed but never dispatched");
                }
                if(i.opcode==99){
                    events.push_back("init");
                    if(init_current_value){
                        require(memory.read(frame,3,41)==100,"Init reads admitted shared purchase price before its first write");
                        require(w.find(frame.context.caller)->object_words.at(41)==0,"constructor's raw CurrentValue word is still zero");
                        require(memory.write(frame,3,41,*init_current_value)&&memory.read(frame,3,41)==init_current_value,
                            "Init CurrentValue write updates the bound candidate value registry");
                    }
                    for(const auto& pair:std::vector<std::pair<std::int16_t,std::int16_t>>{{4,1},{13,128},{23,2},{42,1},{43,10}})
                        require(w.write(frame,3,pair.first,pair.second),"Init writes real constructor flags");
                    require(memory.write(resident,3,0,7),"Init changes another candidate entity");random.next(13);
                }else if(i.opcode==100){
                    events.push_back("ep11");require(memory.write(resident,3,0,11),"EP11 side effect");
                    if(ep11_current_value){
                        require(memory.read(frame,3,41)==init_current_value,"EP11 observes authored Init price before successful buy override");
                        require(memory.write(frame,3,41,*ep11_current_value),"EP11 authored CurrentValue write");
                    }
                    if(ep11_recovers)require(w.set_position(frame.context.caller,{32,32,1,0}),"EP11 recovery changes actual source position");
                }else if(i.opcode==101){
                    events.push_back("ep12");require(w.find(frame.context.caller)!=nullptr,"EP12 precedes removal");
                    require(memory.write(resident,3,0,12),"EP12 side effect");if(host.family_budget)*host.family_budget+=7;
                }else if(i.opcode==102){events.push_back("ep8");}
                else return {};
                PrimitiveOutcome outcome{PrimitiveDisposition::goto_true,{}};outcome.temp_write=VmTempWrite{7,123};return outcome;
            };
        };
        b.placement.architecture=[this](auto,const auto&)->std::optional<ObjectPositionArchitecture>{
            ObjectPositionArchitecture a;a.level_offset=0;a.precise_floor=1;a.out_of_bounds=blocked;return a;};
        b.placement.intersections=[](auto,const auto&,auto)->std::optional<ObjectPositionIntersections>{
            ObjectPositionIntersections i;i.footprint_present=true;i.query.status=RoomInfoObstacleIndexStatus::complete;return i;};
        b.placement.same_group=[](auto a,auto z)->std::optional<bool>{return a.id==z.id&&a.generation==z.generation;};
        b.placement.ignore_intersection=[](auto,auto)->std::optional<bool>{return false;};
        b.placement.ghost_image=[](auto)->std::optional<bool>{return false;};
        b.placement.slot0_height=[](auto)->std::optional<std::int32_t>{return 0;};
        b.on_thread_bound=[this,actual_thread](Vm& thread){
            require(*actual_thread==nullptr,"bind actual candidate thread once");
            if(expected_bound)require(encode_vm_snapshot(thread.snapshot())==encode_vm_snapshot(*expected_bound),
                "selected restore preserves full cross-owner frames and register state exactly");
            *actual_thread=&thread;
        };
        const auto primitive_factory=b.primitive_factory;const auto placement_memory=b.memory;
        b.position_change.dynamic_multitile=[this,&w,&resource,actual_thread,primitive_factory,placement_memory](auto id){
            require(*actual_thread!=nullptr,"EP8 uses actual kernel-owned candidate thread");
            const auto action=resource.runtime.entry_points.entries[8].action;
            require(w.find(id.id)&&w.find(id.id)->generation==id.generation,"EP8 actual candidate identity");
            auto memory=placement_memory;const auto primitive=primitive_factory(memory);
            const auto global=(*actual_thread)->shared_program_for_owner(0);
            const auto bank=(*actual_thread)->shared_program_for_owner(resource.origin.guid);
            require(global&&bank,"EP8 uses actual thread immutable banks");
            Vm check(global,ShareVmProgramTag{},memory,{},primitive,
                SharedRoutineOwners{{resource.origin.guid,bank}},ShareVmOwnerProgramsTag{});
            require(check.start(routine_key(action),{id.id,id.id,0,resource.origin.guid},{0,0,0,0},true)==VmStatus::running,
                "real placement EP8 check frame");
            const auto run=check.run_synchronous_check_with_registers(**actual_thread,false,4096,primitive);
            return run.started&&(run.status==VmStatus::returned_true||run.status==VmStatus::returned_false);
        };
        auto registrations=std::make_shared<std::map<std::int16_t,std::uint16_t>>();
        auto wall_users=std::make_shared<std::set<std::int16_t>>();
        for(const auto& e:w.snapshot().entities)if(e.position){registrations->emplace(e.id,1);wall_users->insert(e.id);}
        b.position_change.story_count=[]()->std::optional<std::int16_t>{return 2;};
        b.position_change.admit_single_ordinary=[&w](auto identity)->std::optional<bool>{
            const auto* e=w.find(identity.id);if(!e||e->generation!=identity.generation)return {};
            const auto flags=e->object_words.find(40);
            return e->kind==EntityKind::object&&flags!=e->object_words.end()&&
                (static_cast<std::uint16_t>(flags->second)&0xc000U)==0&&
                std::none_of(e->slots.begin(),e->slots.end(),[](auto id){return id!=0;});};
        b.position_change.room_at=[](const auto&)->std::optional<std::uint16_t>{return 1;};
        b.position_change.unregister_position=[this,registrations](auto id,bool){
            ++unregister_calls;events.push_back("unregister");registrations->erase(id.id);
            return !fail_unregister||unregister_calls!=fail_unregister;
        };
        b.position_change.detach_container=[this,&w](auto child,auto parent,std::size_t slot){
            const auto* item=w.find(child.id);const auto* support=w.find(parent.id);
            if(fail_detach||!item||!support||item->generation!=child.generation||support->generation!=parent.generation||
               !item->attachment||item->attachment->container!=parent.id||item->attachment->slot!=slot||
               slot>=support->slots.size()||support->slots[slot]!=child.id)return false;
            events.push_back("detach");return w.detach(parent.id,slot);
        };
        b.position_change.lifecycle_phase=[this](auto,ObjectPositionChangePhase phase,const std::function<bool()>& operation){
            if(phase==ObjectPositionChangePhase::pre)++pre_phases;
            return operation();
        };
        b.position_change.register_position=[this,&w,registrations](auto id,std::uint16_t room,bool){
            if(technical_failure)return false;
            require(w.find(id.id)->object_words.at(29)==static_cast<std::int16_t>(room-1),"native Room29 published before registration");
            (*registrations)[id.id]=room;events.push_back("register");return true;};
        b.position_change.wall_use=[this,&w,wall_users](auto id,bool set,bool){
            if(require_dead_cleanup&&!set)require(w.find(id.id)&&w.find(id.id)->generation==id.generation&&w.find(id.id)->dead,
                "source Delete publishes Dead before both PrePositionChange wall-use passes");
            if(set)wall_users->insert(id.id);else wall_users->erase(id.id);events.push_back(set?"wall-set":"wall-clear");return true;};
        return b;
    };}
};
ObjectPurchaseRequest buy_request(){return {2,{ObjectTradeCatalogState::present,0x1234,100},{32,32,1,0}};}
ObjectSaleRequest sell_request(const SessionBundle& s){
    const auto e=std::find_if(s.world.entities.begin(),s.world.entities.end(),[](const auto& e){return e.id==2;});
    require(e!=s.world.entities.end(),"sale fixture target");ObjectSaleRequest request;request.target={2,e->generation};
    request.move_objects=true; // Source cheat bypass tested separately by rule fixtures.
    return request;
}
void success_and_sale(){
    auto s=source();const auto bytes=encode_session_bundle(s);const auto r=resource();Adapter a;
    auto bought=prepare_ts1_object_purchase(s,buy_request(),r,a.factory());prepared(bought,ObjectTradeTransactionStatus::prepared_success,"initial purchase");
    require(encode_session_bundle(s)==bytes,"purchase does not mutate live/source archive");
    require(bought.candidate->host.family_budget==900&&bought.candidate->created_objects.size()==1&&
        bought.candidate->object_trade->version==2&&bought.candidate->object_trade->objects.empty()&&
        bought.candidate->object_trade->groups.front().initial_price==100&&bought.candidate->object_trade->groups.front().members.front().wear==0&&
        bought.candidate->object_trade->groups.front().members.front().disabled==0,
        "purchase debits and persists actual newly-created price/Wear0");
    const auto& created=bought.candidate->world.entities.back();
    require(created.lockout_count&&created.lockout_count->thread_present&&created.lockout_count->timestamp==0,
        "opted-in source clock records actual fresh Thread and known constructor timestamp0");
    require(a.observed_budget==std::vector<std::int32_t>{900,900},"Init and EP11 see debited funds");
    require(bought.candidate->host.random_seed!=s.host.random_seed,"Init RNG retained in prepared session");
    require(bought.candidate->object_threads.size()==1&&bought.candidate->object_threads.front().vm.temps[7]==0&&
        bought.candidate->object_threads.front().vm.temp_xl[0]==0,"idle Thread is saved; external check Temp writes discarded");
    auto live=*bought.candidate;live.object_threads.front().vm.temps[7]=321;live.object_threads.front().vm.temp_xl[0]=123456;
    const auto reopened=decode_session_bundle(encode_session_bundle(live));a.sale_temp=321;a.sale_xl=123456;
    const auto sold=prepare_ts1_object_sale(reopened,sell_request(*bought.candidate),r,a.factory());prepared(sold,ObjectTradeTransactionStatus::prepared_success,"sale after fresh archive");
    require(sold.candidate->host.family_budget==1007&&sold.candidate->world.entities.size()==1&&
        sold.candidate->created_objects.empty()&&sold.candidate->object_trade->groups.empty()&&sold.candidate->object_threads.empty()&&
        sold.candidate->scheduler.entities.size()==1&&
        sold.candidate->object_history->retired_created_objects.size()==1&&sold.candidate->object_history->reserved_entity_ids==std::vector<std::int16_t>{2},
        "sale EP12 then refund, real retirement and canonical history");
    require(a.observed_budget.back()==900,"EP12 observes pre-refund budget");
    require(encode_session_bundle(*bought.candidate)==bought.archive,"sale leaves input purchase unchanged");
    auto replay=buy_request();replay.new_id=2;
    require(prepare_ts1_object_purchase(*sold.candidate,replay,r,a.factory()).status==ObjectTradeTransactionStatus::unsupported,
        "retired ID cannot be reused by a later command");
}
void supported_leaf_sale_cases(){
    const auto r=resource();Adapter buying;
    const auto bought=prepare_ts1_object_purchase(source(),buy_request(),r,buying.factory());
    prepared(bought,ObjectTradeTransactionStatus::prepared_success,"supported sale source purchase");
    auto supported=*bought.candidate;
    World world;require(world.restore_snapshot(supported.world),"supported source World");
    require(world.create(3,EntityKind::object,{0,0,1,0})&&world.define_type(3,0x5678)&&
        world.declare_object_word(3,0,123)&&world.set_position(3,{32,32,1,0})&&world.attach(3,0,2),
        "table with one supported lamp");
    supported.world=world.snapshot();const auto before=encode_session_bundle(supported);
    const auto table=*world.find(3);
    Adapter selling;const auto sold=prepare_ts1_object_sale(supported,sell_request(supported),r,selling.factory());
    prepared(sold,ObjectTradeTransactionStatus::prepared_success,"supported singleton sale");
    World after;require(after.restore_snapshot(sold.candidate->world),"supported sale reopened World");
    const auto* remaining=after.find(3);
    require(!after.find(2)&&remaining&&remaining->generation==table.generation&&remaining->definition&&
        remaining->definition->guid==table.definition->guid&&remaining->position==table.position&&
        remaining->object_words==table.object_words&&!remaining->attachment&&remaining->slots==std::vector<std::int16_t>{0}&&
        sold.candidate->host.family_budget==1007&&sold.candidate->object_trade->groups.empty()&&
        encode_session_bundle(supported)==before,
        "selling supported lamp preserves table, frees only its slot, refunds once and leaves source untouched");
    require(selling.pre_phases==2&&selling.events==std::vector<std::string>{"ep12","unregister","detach","unregister","wall-clear"},
        "supported Delete detaches during first guarded Pre and clears walls only during second Pre");
    for(unsigned failure=0;failure<2;++failure){
        Adapter reject;reject.fail_detach=failure==0;reject.fail_unregister=failure==1?2:0;
        const auto result=prepare_ts1_object_sale(supported,sell_request(supported),r,reject.factory());
        require(!result.candidate&&result.archive.empty()&&encode_session_bundle(supported)==before,
            "failed detach or second Pre rolls back World, slot, funds and archive");
    }
    auto stale=sell_request(supported);++stale.target.generation;Adapter reject_stale;
    require(!prepare_ts1_object_sale(supported,stale,r,reject_stale.factory()).candidate&&reject_stale.binds==0,
        "stale supported generation rejected before callbacks");
    for(unsigned broken=0;broken<2;++broken){
        auto invalid=supported;
        auto parent=std::find_if(invalid.world.entities.begin(),invalid.world.entities.end(),[](const auto& e){return e.id==3;});
        auto child=std::find_if(invalid.world.entities.begin(),invalid.world.entities.end(),[](const auto& e){return e.id==2;});
        if(broken==0)parent->slots[0]=0;else child->attachment->container=1;
        Adapter reject;const auto result=prepare_ts1_object_sale(invalid,sell_request(invalid),r,reject.factory());
        require(!result.candidate&&result.archive.empty()&&reject.binds==0,"broken support reciprocity rejected before callbacks");
    }
    auto with_child=supported;
    auto lamp=std::find_if(with_child.world.entities.begin(),with_child.world.entities.end(),[](const auto& e){return e.id==2;});
    auto nested=with_child.world.entities.back();nested.id=4;nested.generation=++with_child.world.generation_sequence;
    nested.slots.clear();nested.attachment=Attachment{2,0,true};lamp->slots={4};with_child.world.entities.push_back(std::move(nested));
    Adapter reject_children;
    require(!prepare_ts1_object_sale(with_child,sell_request(with_child),r,reject_children.factory()).candidate&&reject_children.binds==0,
        "supported item with children remains unsupported before lifecycle execution");
}
void reused_creation_history_cases(){
    auto s=source();auto r=resource();s.object_history->reusable_entity_ids=true;
    // Identity sequence observed in the native UI save that exposed this bug.
    // Resources remain synthetic: only numeric historical lifetimes are used.
    const std::vector<std::pair<std::int16_t,std::uint64_t>> lifetimes{
        {76,248},{263,266},{264,267},{265,268},{266,269},{267,270},{270,273},{271,274},
        {272,276},{273,277},{275,279},{276,280},{279,283},{281,285},{290,293},{291,294},
        {293,296},{294,297},{295,298},{299,301}};
    for(const auto& lifetime:lifetimes){auto origin=r.origin;origin.id=lifetime.first;origin.generation=lifetime.second;
        s.object_history->retired_created_objects.push_back(std::move(origin));}
    s.world.generation_sequence=313;
    const auto source_bytes=encode_session_bundle(s);
    auto request=buy_request();request.new_id=264;Adapter buying;
    const auto bought=prepare_ts1_object_purchase(s,request,r,buying.factory());
    prepared(bought,ObjectTradeTransactionStatus::prepared_success,"native reused numeric ID purchase");
    require(bought.candidate->created_objects.front().id==264&&bought.candidate->created_objects.front().generation==314,
        "fresh constructor retains exact reused ID264 with distinct current generation314");
    ObjectSaleRequest admission;admission.target={264,314};admission.move_objects=true;Adapter selling;
    const auto sold=prepare_ts1_object_sale(*bought.candidate,admission,r,selling.factory());
    prepared(sold,ObjectTradeTransactionStatus::prepared_success,"native reused numeric ID sale");
    const auto& history=*sold.candidate->object_history;
    require(history.reusable_entity_ids&&history.retired_created_objects.size()==lifetimes.size()+1&&
        history.reserved_entity_ids==std::vector<std::int16_t>{264},
        "sale preserves reusable history and the transaction kernel's explicit retirement reservation");
    std::vector<std::pair<std::int16_t,std::uint64_t>> result;
    for(const auto& origin:history.retired_created_objects)result.emplace_back(origin.id,origin.generation);
    auto expected=lifetimes;expected.insert(expected.begin()+3,{264,314});
    require(result==expected&&encode_session_bundle(s)==source_bytes&&encode_session_bundle(*bought.candidate)==bought.archive,
        "sale keeps all prior lifetimes in lexicographic order and leaves both source archives unchanged");
}
void move_rotation_cases(){
    auto r=resource();Adapter purchase;const auto bought=prepare_ts1_object_purchase(source(),buy_request(),r,purchase.factory());
    prepared(bought,ObjectTradeTransactionStatus::prepared_success,"move source purchase");const auto original=encode_session_bundle(*bought.candidate);
    auto admission=sell_request(*bought.candidate);Adapter moving;
    const auto moved=prepare_ts1_object_move(*bought.candidate,{admission,{48,48,1,7}},r,moving.factory());
    prepared(moved,ObjectTradeTransactionStatus::prepared_success,"existing move/rotation");
    const auto target=std::find_if(moved.candidate->world.entities.begin(),moved.candidate->world.entities.end(),[](const auto& e){return e.id==2;});
    require(target!=moved.candidate->world.entities.end()&&target->position==std::optional<EntityPosition>{{48,48,1,6}},"move uses actual quantized requested heading");
    require(moved.candidate->host.family_budget==bought.candidate->host.family_budget&&moved.candidate->object_trade->groups.front().initial_price==100&&
        encode_session_bundle(*bought.candidate)==original,"move preserves source finances, ledger quote and live input");
    require(moving.events==std::vector<std::string>{"unregister","wall-clear","register","wall-set","ep8","ep11"},"move executes source clear→placement→EP8→EP11 without Init/debit");
    Adapter blocked;blocked.blocked=true;const auto denied=prepare_ts1_object_move(*bought.candidate,{admission,{48,48,1,6}},r,blocked.factory());
    prepared(denied,ObjectTradeTransactionStatus::prepared_authored_refusal,"blocked existing move");
    const auto old=std::find_if(denied.candidate->world.entities.begin(),denied.candidate->world.entities.end(),[](const auto& e){return e.id==2;});
    require(old->position==std::optional<EntityPosition>{{32,32,1,0}}&&std::find(blocked.events.begin(),blocked.events.end(),"ep11")==blocked.events.end(),"blocked move keeps old pose and does not run EP11");
    auto stale=admission;++stale.target.generation;Adapter bad;
    require(prepare_ts1_object_move(*bought.candidate,{stale,{48,48,1,0}},r,bad.factory()).status==ObjectTradeTransactionStatus::unsupported&&bad.binds==0,"stale move identity fails before candidate adapters");
}
void refusals_and_unknown(){
    {auto legacy=source(false);Adapter adapter;const auto bought=prepare_ts1_object_purchase(legacy,buy_request(),resource(),adapter.factory());
        prepared(bought,ObjectTradeTransactionStatus::prepared_success,"legacy purchase");
        require(!bought.candidate->world.lockout_count_clock&&!bought.candidate->world.entities.back().lockout_count,
            "legacy raw OD25 profile is preserved; no global scheduler clock or unknown old timestamps are invented");}

    auto s=source();const auto original=encode_session_bundle(s);auto r=resource();Adapter a;a.blocked=true;
    const auto refused=prepare_ts1_object_purchase(s,buy_request(),r,a.factory());prepared(refused,ObjectTradeTransactionStatus::prepared_authored_refusal,"authored blocked placement");
    require(refused.candidate->host.family_budget==1000&&refused.candidate->world.entities.size()==1&&
        refused.candidate->world.entities.front().object_words.at(0)==11&&
        refused.candidate->host.random_seed!=s.host.random_seed&&refused.candidate->object_history->retired_created_objects.size()==1,
        "authored placement refusal refunds but preserves Init/EP11 effects and burned identity");
    require(refused.diagnostic.find("purchase authored refusal: placement=blocked error=")!=std::string::npos&&
        refused.diagnostic.find("in_world_before_user_entry=false")!=std::string::npos,
        "refusal reports real placement result separately from later user-entry effects");
    require(encode_session_bundle(s)==original,"refusal leaves immutable source untouched until host publication");
    a.ep11_recovers=true;
    prepared(prepare_ts1_object_purchase(s,buy_request(),r,a.factory()),ObjectTradeTransactionStatus::prepared_success,"EP11 recovers blocked placement");
    Adapter technical;technical.technical_failure=true;
    const auto partial=prepare_ts1_object_purchase(s,buy_request(),r,technical.factory());
    require(partial.status==ObjectTradeTransactionStatus::unsupported&&!partial.candidate&&!partial.archive.size()&&
        encode_session_bundle(s)==original,"incomplete native lifecycle is technical failure, publishes no guessed transaction");
    Adapter expired;expired.authorized=false;
    require(!prepare_ts1_object_purchase(s,buy_request(),r,expired.factory()).candidate&&expired.events.empty(),"expired real command has no effects");
    Adapter poor;s.host.family_budget=99;
    require(prepare_ts1_object_purchase(s,buy_request(),r,poor.factory()).status==ObjectTradeTransactionStatus::rejected_preflight&&poor.binds==0,
        "insufficient funds rejects before factory/Init");
    s.host.family_budget=1000;r.runtime.entry_points.entries[1].action=4096;
    prepared(prepare_ts1_object_purchase(s,buy_request(),r,poor.factory()),ObjectTradeTransactionStatus::prepared_success,"ordinary Main can now be staged");
    poor.binds=0;
    r=resource();r.runtime.definition.level_offset=1;
    require(!prepare_ts1_object_purchase(s,buy_request(),r,poor.factory()).candidate&&poor.binds==0,"EP11 architectural floor wrapper refused before Init");
    r.runtime.definition.level_offset.reset();
    require(!prepare_ts1_object_purchase(s,buy_request(),r,poor.factory()).candidate&&poor.binds==0,"unknown LevelOffset cannot be assumed ordinary");
    r=resource();Adapter good;auto bought=prepare_ts1_object_purchase(s,buy_request(),r,good.factory());prepared(bought,ObjectTradeTransactionStatus::prepared_success,"permission fixture purchase");
    auto unknown=*bought.candidate;unknown.object_trade.reset();
    require(!prepare_ts1_object_sale(unknown,sell_request(unknown),r,good.factory()).candidate,"unknown original price never inferred from ObjectData41");
    auto stale=sell_request(*bought.candidate);stale.target.generation++;
    require(!prepare_ts1_object_sale(*bought.candidate,stale,r,good.factory()).candidate,"stale sale identity cannot delete replacement");
    auto denied=sell_request(*bought.candidate);denied.move_objects=false;denied.groups={{true,{denied.target}}};
    denied.selected_disabled=1;denied.avatar_threads_complete=true;Vm actor(RoutineSet{});
    denied.avatar_threads={{{1,bought.candidate->world.entities.front().generation},&actor}};
    denied.buildable=ObjectTradeBuildableArea{8,8,2,0,0,8,8,{}};
    auto disabled_source=*bought.candidate;disabled_source.object_trade->groups.front().members.front().disabled=std::uint8_t{1};
    denied.selected_disabled=0; // Caller cannot override authenticated runtime Disabled1.
    require(prepare_ts1_object_sale(disabled_source,denied,r,good.factory()).status==ObjectTradeTransactionStatus::rejected_preflight,
        "real permission refusal cannot run EP12 or refund");
    denied.selected_disabled=1;
    prepared(prepare_ts1_object_sale(*bought.candidate,denied,r,good.factory()),ObjectTradeTransactionStatus::prepared_success,
        "known runtime Disabled0 outranks contradictory caller fallback");
    auto legacy=*bought.candidate;const auto& known=legacy.object_trade->groups.front();
    SessionObjectTradeLedger old;old.objects.push_back({known.members.front().object,*known.initial_price,known.members.front().wear});legacy.object_trade=old;
    const auto original_legacy=encode_session_bundle(legacy);
    require(prepare_ts1_object_sale(legacy,denied,r,good.factory()).status==ObjectTradeTransactionStatus::rejected_preflight,
        "unknown legacy Disabled still honors rejecting external proof");
    denied.selected_disabled=0;
    prepared(prepare_ts1_object_sale(legacy,denied,r,good.factory()),ObjectTradeTransactionStatus::prepared_success,
        "unknown legacy Disabled retains admitted external zero proof");
    require(encode_session_bundle(legacy)==original_legacy,"legacy permission probes leave the source archive unchanged");
}

ObjectTradeResource with_main(RoutineScope scope){
    auto r=resource();
    const std::uint16_t main=scope==RoutineScope::global?300:(scope==RoutineScope::semiglobal?8193:5000);
    r.runtime.entry_points.entries[1].action=main;
    Bhav code{0x8002,{{103,1,255,{}},{0,2,255,{}},{104,254,255,{}}},{},Bhav::Metadata{0,6,0,0}};
    if(scope==RoutineScope::semiglobal){
        r.runtime.semiglobal_name="tradefixture";
        r.linked_semiglobal=SessionLinkedSemiglobal{"tradefixture","GameData/Global/Global.far","tradefixture.iff"};
        r.linked_routines.emplace(routine_key(main),std::move(code));
    }else if(scope==RoutineScope::global){
        // Actual global Main plus an EMPTY exact private bank. Complete-bank
        // identity is still required; do not invent an executable private root.
        r.global_routines.emplace(routine_key(main),std::move(code));
        const std::vector<std::pair<std::uint16_t,std::uint16_t>> entries{{0,301},{8,302},{11,303},{12,304}};
        for(const auto& entry:entries){
            const auto old=r.runtime.entry_points.entries[entry.first].action;
            r.global_routines.emplace(routine_key(entry.second),r.runtime.private_routines.at(routine_key(old)));
            r.runtime.private_routines.erase(routine_key(old));r.runtime.entry_points.entries[entry.first].action=entry.second;
        }
    }else r.runtime.private_routines.emplace(routine_key(main),std::move(code));
    return r;
}

// A fresh, fully bound native continuation. This runs the existing scheduler
// and Sleep, not a transaction callback claiming that Main advanced.
struct RestoredMainSession {
    SessionBundle bundle;ObjectTradeResource resource;
    World world;RelationshipState relations;RandomState random;
    VmMemory memory;Vm actor;
    EntityThreadControllers actor_controllers;
    std::unique_ptr<Vm> object;std::unique_ptr<Sleep> sleep;
    EntityVmScheduler scheduler;
    RestoredMainSession(const SessionBundle& saved,ObjectTradeResource loaded)
        :bundle(decode_session_bundle(encode_session_bundle(saved))),resource(std::move(loaded)),
         world(WorldLimits{4096,131072,4096}),relations(saved.relationships.limits),random(saved.host.random_seed),
         memory(world.vm_memory()),actor(RoutineSet{},memory),actor_controllers(world,1),scheduler(saved.scheduler.entity_limit){
        require(world.restore_snapshot(bundle.world)&&relations.restore_snapshot(bundle.relationships),"fresh Main World/relations");
        require(world.install_reserved_entity_ids(bundle.object_history->reserved_entity_ids),"fresh Main reservations");
        require(actor.restore_snapshot(bundle.runtime_entities.front().vm)&&actor_controllers.bind(actor)&&
            actor_controllers.restore_snapshot(bundle.runtime_entities.front().controllers),"fresh resident continuation");
        const auto& thread=bundle.object_threads.front();auto bank=resource.runtime.private_routines;
        bank.insert(resource.linked_routines.begin(),resource.linked_routines.end());
        const VmPrimitive primitive=[this](const VmFrame& frame,const Instruction& instruction,const Ts1Temps& temps,const VmDispatch& dispatch)->PrimitiveOutcome {
            if(instruction.opcode==0)return sleep->primitive()(frame,instruction,temps,dispatch);
            require(!dispatch.check_mode,"Main is normal scheduler execution");
            require(frame.context.caller==2&&frame.context.callee==2&&frame.context.stack_object==0&&
                frame.context.code_owner==resource.origin.guid&&frame.arguments.size()==4&&frame.exact_arguments,
                "real Main source caller/callee/StackObject/owner and exact four-argument ABI");
            VmFrame resident;resident.context.caller=1;
            if(instruction.opcode==103){
                require(frame.arguments==std::vector<std::int16_t>({0,0,0,0})&&temps[7]==0,"source new Main defaults, no leaked check registers");
                require(world.write(resident,3,0,77),"Main performs real post-restore World effect");
                PrimitiveOutcome out{PrimitiveDisposition::goto_true,{}};
                out.write=VmFrameWrite{VmFrameBank::arguments,0,2};out.temp_write=VmTempWrite{7,222};return out;
            }
            if(instruction.opcode==104){
                require(temps[7]==222,"Main register survives real Sleep and fresh session restore");
                require(world.write(resident,3,0,88),"Main continues after actual Sleep");return {PrimitiveDisposition::goto_true,{}};
            }
            return {};
        };
        object=std::make_unique<Vm>(resource.global_routines,memory,VmLimits{},primitive,RoutineOwners{{thread.primary_owner,std::move(bank)}});
        require(object->restore_snapshot(thread.vm),"fresh actual object Main VM");
        sleep=std::make_unique<Sleep>(world,[this]{random.next(1);});
        require(sleep->bind(*object)&&sleep->restore_snapshot(thread.sleep),"fresh actual object Sleep");
        require(scheduler.add(1,actor,[this](auto now){return actor_controllers.advance(now);},
            [this]{return actor_controllers.observe();},[this]{return actor_controllers.notify();})&&
            scheduler.add(2,*object,[this](auto now){return sleep->tick(now);},{},[this]{return sleep->interrupt();})&&
            scheduler.restore_snapshot(bundle.scheduler),"fresh exact scheduler roster and hooks");
    }
    void tick(){
        const auto now=bundle.host.tick+1;
        if(world.lockout_count_clock())require(world.set_lockout_count_clock(static_cast<std::uint32_t>(now)),"advance explicit native clock");
        if(!scheduler.tick(now,100,100))throw std::runtime_error("real Main scheduler: "+scheduler.diagnostic());
        bundle.host.tick=now;bundle.host.random_seed=random.seed();bundle.world=world.snapshot();bundle.relationships=relations.snapshot();
        bundle.runtime_entities.front().vm=actor.snapshot();bundle.runtime_entities.front().controllers=actor_controllers.snapshot();
        bundle.object_threads.front().vm=object->snapshot();bundle.object_threads.front().sleep=sleep->snapshot();bundle.scheduler=scheduler.snapshot();
    }
    SessionBundle checkpoint() const {return decode_session_bundle(encode_session_bundle(bundle));}
};

void main_scheduling_cases(){
    for(const auto scope:{RoutineScope::private_object,RoutineScope::semiglobal,RoutineScope::global}){
        const auto s=source();const auto r=with_main(scope);Adapter adapter;
        adapter.expected_main=routine_key(r.runtime.entry_points.entries[1].action);
        const auto purchased=prepare_ts1_object_purchase(s,buy_request(),r,adapter.factory());
        prepared(purchased,ObjectTradeTransactionStatus::prepared_success,"Main staging scope matrix");
        const auto& thread=purchased.candidate->object_threads.front();
        require(thread.vm.status==VmStatus::running&&thread.vm.steps==0&&thread.vm.run_generation==1&&
            thread.vm.stack.size()==1&&thread.vm.stack.front().pc==0&&thread.vm.stack.front().exact_arguments&&
            thread.vm.stack.front().arguments==std::vector<std::int16_t>({0,0,0,0})&&
            thread.vm.stack.front().key.scope==scope&&thread.sleep.clock==purchased.candidate->host.tick&&
            purchased.candidate->world.entities.front().object_words.at(0)==11,
            "Main is pushed before placement but no instruction/effect executes in preparation");
        RestoredMainSession first(*purchased.candidate,r);first.tick();const auto sleeping=first.checkpoint();
        require(sleeping.host.tick==1&&sleeping.object_threads.front().vm.status==VmStatus::waiting&&
            sleeping.object_threads.front().sleep.job&&sleeping.object_threads.front().vm.temps[7]==222&&
            sleeping.world.entities.front().object_words.at(0)==77,
            "first actual next tick executes Main and parks native Sleep");
        RestoredMainSession second(sleeping,r);
        second.tick();require(second.object->status()==VmStatus::waiting,"fresh restored Sleep is not resumed early");
        second.tick();second.tick();const auto continued=second.checkpoint();
        require(continued.object_threads.front().vm.status==VmStatus::returned_true&&
            !continued.object_threads.front().sleep.job&&continued.world.entities.front().object_words.at(0)==88,
            "fresh canonical restore resumes actual Main/Sleep through completion");
        // Deleting the target's own Main/Sleep is source disposal, not a hard
        // reference from another entity. EP12 clones the actual parked registers.
        Adapter sale;sale.sale_temp=222;
        const auto removed=prepare_ts1_object_sale(sleeping,sell_request(sleeping),r,sale.factory());
        prepared(removed,ObjectTradeTransactionStatus::prepared_success,"source disposal of owned Main/Sleep");
        require(removed.candidate->object_threads.empty()&&removed.candidate->scheduler.entities.size()==1&&
            std::count(sale.events.begin(),sale.events.end(),"unregister")==2&&
            std::count(sale.events.begin(),sale.events.end(),"wall-clear")==2,
            "delete performs both source PrePositionChange passes then removes own thread/job/scheduler");
        Adapter refused;refused.blocked=true;refused.expected_main=adapter.expected_main;
        const auto failed=prepare_ts1_object_purchase(s,buy_request(),r,refused.factory());
        prepared(failed,ObjectTradeTransactionStatus::prepared_authored_refusal,"refusal disposes unexecuted Main");
        require(failed.candidate->object_threads.empty()&&failed.candidate->scheduler.entities.size()==1&&
            failed.candidate->world.entities.front().object_words.at(0)==11,"refusal never runs the staged Main");
    }
    {
        const auto r=with_main(RoutineScope::private_object);Adapter a;
        const auto purchase=prepare_ts1_object_purchase(source(),buy_request(),r,a.factory());
        prepared(purchase,ObjectTradeTransactionStatus::prepared_success,"hard-reference setup purchase");
        auto referenced=*purchase.candidate;World live;
        require(live.restore_snapshot(referenced.world),"queued-reference exact World");
        auto bank=r.runtime.private_routines;bank.insert(r.linked_routines.begin(),r.linked_routines.end());
        Vm actor(r.global_routines,live.vm_memory(),{}, {},RoutineOwners{{r.origin.guid,std::move(bank)}});
        EntityThreadControllers controls(live,1);
        require(actor.restore_snapshot(referenced.runtime_entities.front().vm)&&controls.bind(actor)&&
            controls.restore_snapshot(referenced.runtime_entities.front().controllers),"real actor queue binding");
        // A queue requires an actual live Main; an empty ready VM is not an
        // executing avatar. Start the exact already-installed routine without
        // dispatching it, preserving actor1's own caller/callee identity.
        require(actor.start(routine_key(r.runtime.entry_points.entries[1].action),{1,1,0,r.origin.guid},{0,0,0,0},true)==VmStatus::running,
            "actual other-entity Main frame");
        if(!controls.interactions().enqueue(routine_key(r.runtime.entry_points.entries[1].action),{1,2,2,r.origin.guid}))
            throw std::runtime_error("real other-entity queued hard reference: "+controls.interactions().diagnostic());
        referenced.runtime_entities.front().vm=actor.snapshot();
        auto& scheduled_actor=referenced.scheduler.entities.front();
        scheduled_actor.run_generation=actor.run_generation();scheduled_actor.steps=actor.steps();scheduled_actor.status=actor.status();
        referenced.runtime_entities.front().controllers=controls.snapshot();
        referenced.runtime_entities.front().resource_owners=referenced.object_threads.front().resource_owners;
        const auto bytes=encode_session_bundle(referenced);Adapter denied;
        const auto result=prepare_ts1_object_sale(referenced,sell_request(referenced),r,denied.factory());
        require(result.status==ObjectTradeTransactionStatus::unsupported&&!result.candidate&&denied.events.empty()&&
            encode_session_bundle(referenced)==bytes,"other entity hard reference refuses before EP12; never disposed as target-owned Main");
    }
    auto missing=resource();missing.runtime.entry_points.entries[1].action=7000;Adapter a;
    const auto idle=prepare_ts1_object_purchase(source(),buy_request(),missing,a.factory());
    prepared(idle,ObjectTradeTransactionStatus::prepared_success,"known missing Main routine");
    require(idle.candidate->object_threads.front().vm.stack.empty()&&idle.candidate->object_threads.front().vm.status==VmStatus::ready,
        "source known absent Main remains a real idle thread, not an invented routine or fault");
    auto ep6=with_main(RoutineScope::private_object);ep6.runtime.entry_points.entries[6].action=4096;Adapter executable;
    const auto with_ep6=prepare_ts1_object_purchase(source(),buy_request(),ep6,executable.factory());
    prepared(with_ep6,ObjectTradeTransactionStatus::prepared_success,"EP6-capable ordinary object creation");
    Adapter deleting;deleting.require_dead_cleanup=true;
    const auto deleted=prepare_ts1_object_sale(*with_ep6.candidate,sell_request(*with_ep6.candidate),ep6,deleting.factory());
    prepared(deleted,ObjectTradeTransactionStatus::prepared_success,"real Dead state supports EP6-capable deletion");
    require(deleting.pre_phases==2&&std::count(deleting.events.begin(),deleting.events.end(),"wall-clear")==2,
        "both deletion Pre passes expose the same real Dead owner rather than executing live EP6");
    Adapter failed_dead;failed_dead.require_dead_cleanup=true;failed_dead.fail_unregister=2;
    const auto before_dead=encode_session_bundle(*with_ep6.candidate);
    require(!prepare_ts1_object_sale(*with_ep6.candidate,sell_request(*with_ep6.candidate),ep6,failed_dead.factory()).candidate&&
        encode_session_bundle(*with_ep6.candidate)==before_dead,"failed Dead cleanup publishes no partial lifecycle or source mutation");
}

ObjectTradeResource multipart_resource(std::size_t count){
    auto root=resource();root.origin.guid=0x1100;root.origin.asset="group.iff";
    root.runtime.definition.guid=root.origin.guid;root.runtime.definition.master_id=7;
    root.runtime.definition.sub_index=-1;root.runtime.definition.is_multitile=true;
    auto plan=std::make_shared<ObjectTradeGroupResources>();plan->requested=root.origin;plan->multitile=true;plan->base_index=count-1;
    for(std::size_t n=0;n<count;++n){auto part=resource();part.origin.guid=0x2200+static_cast<std::uint32_t>(n);
        part.origin.asset="group.iff";part.origin.definition_chunk_id=static_cast<std::uint16_t>(129+n);part.origin.master_guid=root.origin.guid;
        part.runtime.definition.guid=part.origin.guid;part.runtime.definition.master_id=7;part.runtime.definition.sub_index=static_cast<std::int16_t>(n);
        part.runtime.definition.is_multitile=true;part.runtime.master_guid=root.origin.guid;part.runtime.entry_points.entries[8].action=0;
        part.runtime.entry_points.entries[1].action=5000;
        part.runtime.private_routines.emplace(routine_key(5000),Bhav{0x8002,{{200,254,255,{}}},{},Bhav::Metadata{0,4,0,0}});
        plan->parts.push_back({std::move(part),static_cast<std::int16_t>(-16+16*n),static_cast<std::int16_t>(n==2?-16:0),0,
            std::uint16_t(n==count-1?1:0)});
    }
    root.group=std::move(plan);return root;
}
ObjectTradeResource existing_multipart_resource(const ObjectTradeResource& purchased,const SessionBundle& source,std::size_t clicked=0){
    auto plan=std::make_shared<ObjectTradeGroupResources>(*purchased.group);
    const auto& group=source.object_trade->groups.front();
    for(std::size_t n=0;n<plan->parts.size();++n)plan->parts[n].resource.origin=group.members[n].object;
    auto selected=plan->parts[clicked].resource;selected.group=std::move(plan);return selected;
}
struct GroupAdapter {
    std::optional<ObjectGroup> physical;std::map<std::int16_t,Vm*> threads;
    std::vector<std::pair<std::int16_t,std::uint16_t>> events;
    std::vector<std::int16_t> init_prices,place_prices;
    bool blocked=false,technical=false,restored=false,deleting=false,actorless_source=false,native_constructor=false;
    std::weak_ptr<DynamicObjectGroups> native_owner;
    ObjectGroupSnapshot* native_snapshot=nullptr;
    std::vector<NativeObjectGroupMember> init_native_fields;
    std::vector<WorldEntityReference> init_native_heads;
    std::int16_t expected_sale_price=100;std::size_t expected_parts=0;unsigned dead_passes=0;
    ObjectTradeBindingFactory factory(){return [this](World& world,RelationshipState&,SessionHostState& host,RandomState&,RuntimeGlobals*,const Ts1Clock*,const ObjectTradeResource& command){
        require(command.group&&command.group->parts.size()==expected_parts,"group factory receives complete immutable resource plan");
        const auto plan=command.group;
        ObjectTradeRuntimeBinding binding;binding.memory=world.vm_memory();binding.command_still_matches=[] {return true;};
        if(native_constructor){
            binding.native_group_construction=NativeObjectGroupConstructionProfile::original_native;
            binding.on_group_state_bound=[this,&world](ObjectGroupSnapshot& groups,ObjectValueRegistry& values){
                native_snapshot=&groups;DynamicObjectGroupServices services;services.authority=[] {return true;};
                services.origin=[&values](auto id)->std::optional<ObjectGroupMemberOrigin>{
                    const auto* group=values.query({id.id,id.generation});if(!group)return {};
                    for(const auto& member:group->members)if(member.object.id==id.id&&member.object.generation==id.generation){const auto& o=member.object;
                        return ObjectGroupMemberOrigin{ObjectGroupMemberSource::native_created,o.guid,o.master_guid,o.source,o.asset,o.definition_chunk_id};}return {};};
                services.publish_native_direction=[&world](WorldEntityReference id,std::uint8_t heading){
                    return world.publish_native_object_heading(id,heading);};
                auto owner=std::make_shared<DynamicObjectGroups>(world,values,groups,std::move(services));native_owner=owner;return owner;
            };
        }
        binding.on_group_materialized=[this,&world](const ObjectGroup& group){physical=group;threads.clear();
            require(group.members.size()==expected_parts,"whole group is published before first member binding");
            for(const auto& member:group.members)require(world.find(member.id)&&world.find(member.id)->generation==member.generation,
                "all physical bodies exist before first Init");};
        binding.on_member_thread_bound=[this,&world](ObjectTradeIdentity id,CodeOwner owner,Vm& vm){
            const auto* e=world.find(id.id);require(e&&e->generation==id.generation&&e->definition&&owner==e->definition->guid&&
                vm.shared_program_for_owner(owner)&&threads.emplace(id.id,&vm).second,"each actual VM binds once to its own exact identity/bank");};
        binding.member_programs=[plan](ObjectTradeIdentity id)->std::optional<ObjectTradeMemberAdmission>{
            for(const auto& part:plan->parts)if(part.resource.origin.id==id.id&&part.resource.origin.generation==id.generation){
                auto bank=part.resource.runtime.private_routines;bank.insert(part.resource.linked_routines.begin(),part.resource.linked_routines.end());
                return ObjectTradeMemberAdmission{{std::make_shared<const VmProgram>(part.resource.global_routines),
                    {{part.resource.origin.guid,std::make_shared<const VmProgram>(std::move(bank))}}},VmSavedRoutinePolicy::require_present};}
            return {};};
        binding.primitive_factory=[this,&world,&host](const VmMemory& memory){return [this,&world,&host,&memory](const VmFrame& frame,const Instruction& instruction,const Ts1Temps& temps,const VmDispatch& dispatch)->PrimitiveOutcome{
            require(physical&&threads.size()==expected_parts&&dispatch.check_mode&&
                frame.arguments==std::vector<std::int16_t>({0,0,0,0}),"all VMs bind before exact synchronous lifecycle checks; Main never runs");
            const auto* caller=world.find(frame.context.caller);require(caller&&caller->definition&&caller->definition->guid==frame.context.code_owner,
                "entry code owner is the exact individual part, never copied from selected sibling");
            const auto expected_temp=restored?static_cast<std::int16_t>(100+caller->id):0;
            require(temps[7]==expected_temp&&dispatch.origin->temp_xl()[0]==(restored?1000+caller->id:0),
                "each lifecycle clones only its own part registers");
            events.push_back({caller->id,instruction.opcode});
            if(instruction.opcode==99){
                if(native_constructor){const auto owner=native_owner.lock();const auto ref=WorldEntityReference{caller->id,caller->generation};
                    require(owner&&native_snapshot,"actual native constructor owner exists before script Init0");
                    const auto head=owner->group_head(ref);require(head.status==NativeObjectGroupLinkStatus::member,"Init sees actual native head");
                    init_native_heads.push_back(head.reference);bool found=false;
                    for(const auto& group:native_snapshot->groups)for(const auto& member:group.members)if(member.id==ref.id&&member.generation==ref.generation){
                        require(member.native.has_value(),"Init sees admitted native constructor fields");init_native_fields.push_back(*member.native);found=true;}
                    require(found,"Init reads same physical snapshot owner");}
                for(const auto& member:physical->members)require(world.find(member.id),"first Init can find every sibling");
                const auto price=memory.read(frame,3,41);require(price.has_value(),"Init reads admitted shared value");init_prices.push_back(*price);
                if(caller->id==physical->members.front().id)require(memory.write(frame,3,41,40),"Init writes shared price before next sibling Init");
                for(const auto& field:std::vector<std::pair<std::int16_t,std::int16_t>>{{4,1},{13,128},{23,2},{42,1},{43,10}})
                    require(world.write(frame,3,field.first,field.second),"Init establishes actual ordinary placement flags");
                VmFrame resident;resident.context.caller=1;const auto before=memory.read(resident,3,0);
                require(before&&memory.write(resident,3,0,static_cast<std::int16_t>(*before+1)),"each Init leaves an observable shared World effect");
            }else if(instruction.opcode==100){
                const auto price=memory.read(frame,3,41);require(price.has_value(),"EP11 reads shared authored price");place_prices.push_back(*price);
                if(caller->id==physical->members.front().id)require(memory.write(frame,3,41,90),"EP11 writes shared price visible to later parts");
            }else if(instruction.opcode==101){
                require(memory.read(frame,3,41)==expected_sale_price,"sale computes group refund before selected EP12 changes value");
                require(memory.write(frame,3,41,5),"clicked EP12 may change group price without changing captured refund");
                if(host.family_budget)*host.family_budget+=7;
                else require(actorless_source,"unknown family absence cannot be supplied as a fake group budget");
            }else throw std::runtime_error("unexpected authored group opcode or early Main execution");
            PrimitiveOutcome result{PrimitiveDisposition::goto_true,{}};result.temp_write=VmTempWrite{7,777};return result;
        };};
        binding.placement.architecture=[this](ObjectPositionIdentity id,const EntityPosition&)->std::optional<ObjectPositionArchitecture>{
            ObjectPositionArchitecture result;result.level_offset=0;result.precise_floor=1;
            result.out_of_bounds=blocked&&physical&&id.id==physical->members.back().id;return result;};
        binding.placement.intersections=[](auto,const auto&,auto)->std::optional<ObjectPositionIntersections>{
            ObjectPositionIntersections result;result.footprint_present=true;result.query.status=RoomInfoObstacleIndexStatus::complete;return result;};
        binding.placement.same_group=[this](auto a,auto b)->std::optional<bool>{
            const auto has=[&](auto id){return physical&&std::any_of(physical->members.begin(),physical->members.end(),[&](const auto& member){return member.id==id.id&&member.generation==id.generation;});};return has(a)&&has(b);};
        binding.placement.ignore_intersection=[](auto,auto)->std::optional<bool>{return false;};
        binding.placement.ghost_image=[](auto)->std::optional<bool>{return false;};
        binding.placement.slot0_height=[](auto)->std::optional<std::int32_t>{return 0;};
        auto& change=binding.position_change;change.story_count=[]()->std::optional<std::int16_t>{return 2;};
        change.group=[this](ObjectPositionIdentity id)->std::optional<ObjectGroup>{
            if(native_constructor&&native_snapshot)for(const auto& group:native_snapshot->groups)for(const auto& member:group.members)
                if(member.id==id.id&&member.generation==id.generation)return group;
            if(physical&&std::any_of(physical->members.begin(),physical->members.end(),[&](const auto& member){return member.id==id.id&&member.generation==id.generation;}))return physical;return {};};
        change.authenticate_imported_group=[this](const ObjectGroup& group){return physical&&group.definition_guid==0&&physical->definition_guid==0&&
            group.base_id==physical->base_id&&group.members.size()==physical->members.size();};
        const auto admit=[this,&world](ObjectPositionIdentity id)->std::optional<bool>{const auto* e=world.find(id.id);
            return e&&e->generation==id.generation&&e->definition&&physical&&
                std::any_of(physical->members.begin(),physical->members.end(),[&](const auto& member){return member.id==id.id&&member.generation==id.generation;});};
        change.admit_single_ordinary=admit;change.admit_group_member=admit;
        if(native_constructor){
            change.authenticate_live_group=[this,&world](const ObjectGroup& group){
                if(!native_snapshot||group.kind!=ObjectGroupKind::live_association||!group.native_head)return false;
                validate_object_groups(*native_snapshot,world.snapshot());return true;};
            change.begin_native_rotation=[this](auto id,auto heading)->std::optional<NativeObjectGroupRotation>{const auto owner=native_owner.lock();return owner?owner->begin_native_rotation(id,heading):std::nullopt;};
            change.rollback_native_rotation=[this](const auto& rotation){const auto owner=native_owner.lock();return owner&&owner->rollback_native_rotation(rotation);};
            change.native_current_offset=[this](auto ref)->std::optional<NativeObjectGroupOffset>{const auto owner=native_owner.lock();return owner?owner->native_offset(ref):std::nullopt;};
            change.native_group_head=[this](auto ref){const auto owner=native_owner.lock();return owner?owner->group_head(ref):NativeObjectGroupLink{};};
            change.native_group_next=[this](auto ref){const auto owner=native_owner.lock();return owner?owner->group_next(ref):NativeObjectGroupLink{};};
        }
        change.room_at=[](const auto&)->std::optional<std::uint16_t>{return 1;};
        change.unregister_position=[](auto,bool){return true;};
        change.register_position=[this](auto id,auto room,bool){return !technical||room==0||!physical||id.id!=physical->members.back().id;};
        change.wall_use=[this,&world](auto id,bool set,bool){
            if(deleting&&!set){require(world.find(id.id)&&world.find(id.id)->dead,"multipart Delete exposes Dead before source wall-use cleanup");++dead_passes;}
            return true;};
        return binding;
    };}
};
ObjectSaleRequest group_sale_request(const SessionBundle& source,std::size_t clicked=0,const Vm* actual_avatar=nullptr){
    const auto& group=source.object_trade->groups.front();ObjectSaleRequest request;
    request.target={group.members[clicked].object.id,group.members[clicked].object.generation};request.avatar_threads_complete=true;
    for(const auto& entity:source.world.entities)if(entity.kind==EntityKind::avatar){
        const auto saved=std::find_if(source.runtime_entities.begin(),source.runtime_entities.end(),[&](const auto& actor){return actor.id==entity.id;});
        if(!actual_avatar||saved==source.runtime_entities.end()||encode_vm_snapshot(actual_avatar->snapshot())!=encode_vm_snapshot(saved->vm))
            request.avatar_threads_complete=false;
        else request.avatar_threads.push_back({{entity.id,entity.generation},actual_avatar});
    }
    request.buildable=ObjectTradeBuildableArea{64,64,2,0,0,64,64,{}};ObjectTradeGroup physical;physical.complete=true;
    for(const auto& member:group.members)physical.members.push_back({member.object.id,member.object.generation});request.groups.push_back(std::move(physical));return request;
}
void native_purchase_constructor_cases(){
    auto r=multipart_resource(2);auto plan=std::make_shared<ObjectTradeGroupResources>(*r.group);
    // The actual definitions use authored (0,0) and (0,1); the value lead is
    // the SECOND body. Native parent remains first actual constructor ID8.
    plan->parts[0].offset_x=plan->parts[1].offset_x=0;
    plan->parts[0].offset_y=0;plan->parts[1].offset_y=16;r.group=plan;
    const std::int32_t expected_x[]{0,-1,0,1},expected_y[]{1,0,-1,0};
    for(std::size_t n=0;n<4;++n){auto initial=source();initial.scheduler.entity_limit=8;
        ObjectPurchaseRequest request{8,{ObjectTradeCatalogState::present,r.origin.guid,100},{160,160,1,std::uint8_t(n*2)},{8,2}};
        GroupAdapter actual;actual.expected_parts=2;actual.native_constructor=true;
        const auto bought=prepare_ts1_object_purchase(initial,request,r,actual.factory());
        prepared(bought,ObjectTradeTransactionStatus::prepared_success,"actual native constructor before Init matrix");
        require(actual.init_native_fields.size()==2&&actual.init_native_heads.size()==2&&
            actual.init_native_heads[0].id==8&&actual.init_native_heads[1]==actual.init_native_heads[0]&&
            actual.init_native_fields[0].current.x==0&&actual.init_native_fields[0].current.y==0&&
            actual.init_native_fields[1].authored.x==0&&actual.init_native_fields[1].authored.y==1&&
            actual.init_native_fields[1].current.x==expected_x[n]&&actual.init_native_fields[1].current.y==expected_y[n],
            "real script Init callbacks see native fields recomposed before first Init, independently of value BaseObject");
        const auto& owner=bought.candidate->object_groups->groups.front();
        require(owner.native_head&&owner.native_head->id==8&&owner.base_id==2&&owner.members[0].id==8&&owner.members[1].id==2&&
            owner.members[0].native->next==WorldEntityReference{2,owner.members[1].generation}&&
            owner.members[1].native->next==WorldEntityReference{}&&bought.candidate->object_trade->groups.front().initial_price==100&&
            encode_object_group_snapshot(*bought.candidate->object_groups)[0]==3,
            "native constructor persists actual chain/head with one existing shared price owner");
        if(n==0){auto second=request;second.new_id=9;second.new_ids={9,3};GroupAdapter legacy;legacy.expected_parts=2;
            const auto denied=prepare_ts1_object_purchase(*bought.candidate,second,r,legacy.factory());
            require(!denied.candidate&&denied.diagnostic.find("known and unknown compatible")!=std::string::npos&&legacy.events.empty(),
                "unknown legacy constructor cannot introduce a mixed compatible native cohort before Init");}
    }
    auto initial=source();initial.scheduler.entity_limit=8;
    ObjectPurchaseRequest request{8,{ObjectTradeCatalogState::present,r.origin.guid,100},{160,160,1,0},{8,2}};
    GroupAdapter legacy;legacy.expected_parts=2;
    const auto old=prepare_ts1_object_purchase(initial,request,r,legacy.factory());
    prepared(old,ObjectTradeTransactionStatus::prepared_success,"unchanged unknown legacy constructor profile");
    require(old.candidate->object_groups&&!old.candidate->object_groups->groups.front().native_head,
        "legacy profile never invents actual native metadata from old construction offsets");
    const auto before=encode_session_bundle(*old.candidate);request.new_id=9;request.new_ids={9,3};
    GroupAdapter native;native.expected_parts=2;native.native_constructor=true;
    const auto denied=prepare_ts1_object_purchase(*old.candidate,request,r,native.factory());
    require(!denied.candidate&&denied.diagnostic.find("known and unknown compatible")!=std::string::npos&&native.events.empty()&&
        encode_session_bundle(*old.candidate)==before,"native profile rejects unknown compatible legacy peers without source mutation or Init");
    // Direct selection of a MT leaf has no group resource wrapper, but its
    // actual class still owns authored fields and a singleton native chain.
    auto direct=plan->parts.front().resource;direct.origin.master_guid.reset();direct.runtime.master_guid.reset();
    direct.runtime.definition.sub_index=0x02fd;direct.lead_object=0;
    GroupAdapter single;single.expected_parts=1;single.native_constructor=true;const auto actual_factory=single.factory();
    const ObjectTradeBindingFactory singleton_factory=[actual_factory](World& world,RelationshipState& relations,SessionHostState& host,
            RandomState& random,RuntimeGlobals* globals,const Ts1Clock* clock,const ObjectTradeResource& leaf){
        auto command=leaf;auto fixture_plan=std::make_shared<ObjectTradeGroupResources>();fixture_plan->requested=leaf.origin;
        fixture_plan->parts.push_back({leaf,0,0,0,std::uint16_t{0}});command.group=std::move(fixture_plan);
        return actual_factory(world,relations,host,random,globals,clock,command);
    };
    ObjectPurchaseRequest single_request{8,{ObjectTradeCatalogState::present,direct.origin.guid,100},{160,160,1,0},{}};
    const auto singleton=prepare_ts1_object_purchase(source(),single_request,direct,singleton_factory);
    prepared(singleton,ObjectTradeTransactionStatus::prepared_success,"actual direct MT singleton constructor");
    require(single.init_native_fields.size()==1&&single.init_native_fields[0].authored.x==2&&single.init_native_fields[0].authored.y==253&&
        single.init_native_fields[0].current.x==0&&single.init_native_fields[0].current.y==0&&
        singleton.candidate->object_groups->groups.front().native_head->id==8,
        "direct MT constructor preserves unsigned authored Y while singleton current offset is relative zero");
}
void multipart_trade_matrix(){
    for(std::size_t count:{2U,3U})for(std::uint8_t direction:{0,2,4,6}){
        auto initial=source();Ts1Clock clock;require(clock.initialize(9,4,12,7,2020),"exact source construction clock");
        initial.simulation=SessionSimulationState{clock.snapshot(),{{1,{}}}};
        const auto initial_bytes=encode_session_bundle(initial);const auto resource=multipart_resource(count);
        ObjectPurchaseRequest request{2,{ObjectTradeCatalogState::present,resource.origin.guid,100},{160,160,1,direction},{}};
        for(std::size_t n=0;n<count;++n)request.new_ids.push_back(static_cast<std::int16_t>(2+n));
        GroupAdapter buying;buying.expected_parts=count;
        const auto bought=prepare_ts1_object_purchase(initial,request,resource,buying.factory());prepared(bought,ObjectTradeTransactionStatus::prepared_success,"multipart purchase matrix");
        const auto& group=bought.candidate->object_trade->groups.front();
        require(bought.candidate->host.family_budget==900&&group.members.size()==count&&group.initial_price==100&&
            group.base->id==static_cast<std::int16_t>(count+1)&&bought.candidate->object_threads.size()==count&&
            bought.candidate->object_groups&&bought.candidate->object_groups->groups.front().base_id==group.base->id,
            "purchase charges once and persists ordered physical/value group with nonfirst BaseObject");
        require(buying.init_prices.size()==count&&buying.init_prices.front()==100&&
            std::all_of(buying.init_prices.begin()+1,buying.init_prices.end(),[](auto value){return value==40;})&&
            buying.place_prices.size()==count&&buying.place_prices.front()==40,
            "all parts see shared Init writes before success restores catalog price");
        for(std::size_t n=0;n<count;++n){const auto& member=group.members[n];const auto& offset=resource.group->parts[n];
            auto dx=int(offset.offset_x)-resource.group->parts.back().offset_x,dy=int(offset.offset_y)-resource.group->parts.back().offset_y;
            const auto original_x=dx;switch(direction){case 2:dx=-dy;dy=original_x;break;case 4:dx=-dx;dy=-dy;break;case 6:dx=dy;dy=-original_x;break;}
            const auto entity=std::find_if(bought.candidate->world.entities.begin(),bought.candidate->world.entities.end(),[&](const auto& e){return e.id==member.object.id;});
            require(entity!=bought.candidate->world.entities.end()&&entity->position&&entity->position->x==160+dx&&entity->position->y==160+dy&&
                entity->position->direction==direction,"four rotations preserve negative offsets relative to actual source base");
            require(entity->object_words.at(45)==2020&&entity->object_words.at(46)==7&&entity->object_words.at(47)==4&&
                entity->object_words.at(24)==9&&entity->object_words.at(21)==12,"each part's birth fields are sampled after constructor OOW placement");
            require(bought.candidate->object_threads[n].vm.steps==0&&bought.candidate->object_threads[n].vm.stack.size()==1,
                "each distinct Main is staged but never executed in purchase");
            require(buying.events[n]==std::make_pair(member.object.id,std::uint16_t{99})&&
                buying.events[count+n]==std::make_pair(member.object.id,std::uint16_t{100}),"Init and group EP11 follow complete source order");
        }
        auto live=*bought.candidate;for(auto& thread:live.object_threads){thread.vm.temps[7]=static_cast<std::int16_t>(100+thread.id);thread.vm.temp_xl[0]=1000+thread.id;}
        const auto before_move=encode_session_bundle(live);const auto selected=existing_multipart_resource(resource,live);
        Vm actual_avatar(RoutineSet{});require(actual_avatar.restore_snapshot(live.runtime_entities.front().vm),
            "restore the fixture's exact actual avatar Thread for source IsInUse permission proof");
        GroupAdapter moving;moving.expected_parts=count;moving.restored=true;
        const auto moved=prepare_ts1_object_move(live,{group_sale_request(live,0,&actual_avatar),{192,192,1,std::uint8_t((direction+2)%8)}},selected,moving.factory());
        prepared(moved,ObjectTradeTransactionStatus::prepared_success,"multipart move matrix");
        require(moved.candidate->host.family_budget==900&&moving.events.size()==count,"move executes EP11 once per part without another charge");
        for(std::size_t n=0;n<count;++n)require(encode_vm_snapshot(moved.candidate->object_threads[n].vm)==encode_vm_snapshot(live.object_threads[n].vm),
            "movement retains each part's own frame/register state");
        // Move's authored EP11 writes are not reset like successful purchase.
        require(moved.candidate->object_trade->groups.front().initial_price==90,"move preserves authored shared-price change");
        GroupAdapter selling;selling.expected_parts=count;selling.restored=true;selling.deleting=true;
        const auto sold=prepare_ts1_object_sale(live,group_sale_request(live,0,&actual_avatar),selected,selling.factory());
        prepared(sold,ObjectTradeTransactionStatus::prepared_success,"multipart static sale matrix");
        require(sold.candidate->host.family_budget==1007&&sold.candidate->world.entities.size()==1&&
            sold.candidate->object_trade->groups.empty()&&sold.candidate->object_threads.empty()&&sold.candidate->object_groups->groups.empty()&&
            selling.events==std::vector<std::pair<std::int16_t,std::uint16_t>>{{2,101}}&&selling.dead_passes==2*count,
            "static sale executes EP12 only on clicked nonbase part, deletes whole group and refunds pre-EP12 price once");
        require(encode_session_bundle(live)==before_move&&encode_session_bundle(initial)==initial_bytes,"complete matrix never mutates either input archive");
        auto immovable=live;auto nonbase=std::find_if(immovable.world.entities.begin(),immovable.world.entities.end(),[](const auto& e){return e.id==2;});
        nonbase->object_words[43]=0;GroupAdapter low_level;low_level.expected_parts=count;low_level.restored=true;
        const auto moved_flagged=prepare_ts1_object_move(immovable,{group_sale_request(immovable,0,&actual_avatar),{208,208,1,0}},selected,low_level.factory());
        prepared(moved_flagged,ObjectTradeTransactionStatus::prepared_success,"TS1 move command does not import TSO clicked-piece mobility filter");
        GroupAdapter delete_filtered;delete_filtered.expected_parts=count;
        require(prepare_ts1_object_sale(immovable,group_sale_request(immovable,0,&actual_avatar),selected,delete_filtered.factory()).status==ObjectTradeTransactionStatus::rejected_preflight&&
            delete_filtered.events.empty(),"TS1 delete still enforces clicked/group IsUserMovable without MoveObjects cheat");
        GroupAdapter blocked_move;blocked_move.expected_parts=count;blocked_move.blocked=true;blocked_move.restored=true;
        const auto refused_move=prepare_ts1_object_move(live,{group_sale_request(live,0,&actual_avatar),{240,240,1,direction}},selected,blocked_move.factory());
        prepared(refused_move,ObjectTradeTransactionStatus::prepared_authored_refusal,"late-member movement refusal");
        require(blocked_move.events.empty()&&refused_move.candidate->host.family_budget==900&&
            refused_move.candidate->object_trade->groups.front().initial_price==100,"blocked group move does not execute EP11, refund, debit or reset authored price");
        if(count==2&&direction==0){
            auto worn=live;worn.object_trade->groups.front().members.back().wear=20;
            GroupAdapter selling_worn;selling_worn.expected_parts=count;selling_worn.restored=true;selling_worn.deleting=true;
            const auto worn_sale=prepare_ts1_object_sale(worn,group_sale_request(worn,0,&actual_avatar),selected,selling_worn.factory());
            prepared(worn_sale,ObjectTradeTransactionStatus::prepared_success,"actual nonfirst base Wear controls one refund");
            // Pinned VMMultitileGroup.Price divides Wear by400, not100:
            // InitialPrice100 and base Wear20 refund95, while clicked Wear0
            // would refund100. EP12 adds its independent7 before that refund.
            const auto canonical_refund=ts1_object_refund_value(100,true,std::uint16_t{20});
            const auto clicked_refund=ts1_object_refund_value(100,true,std::uint16_t{0});
            require(worn.object_trade->groups.front().initial_price==100&&
                worn.object_trade->groups.front().base->id==worn.object_trade->groups.front().members.back().object.id&&
                canonical_refund.amount==95&&clicked_refund.amount==100&&
                worn_sale.candidate->host.family_budget==*worn.host.family_budget+7+*canonical_refund.amount,
                "refund uses last lead/base Wear20 with source denominator400 rather than first clicked member Wear0");
            auto imported=live;imported.created_objects.clear();imported.object_groups.reset();
            auto imported_plan=std::make_shared<ObjectTradeGroupResources>(*selected.group);imported_plan->requested.reset();imported_plan->imported=true;
            for(std::size_t n=0;n<count;++n){const auto master=std::uint32_t(0x3300+n);
                auto& value=imported.object_trade->groups.front().members[n];value.object.master_guid=master;
                auto entity=std::find_if(imported.world.entities.begin(),imported.world.entities.end(),[&](const auto& e){return e.id==value.object.id;});entity->definition->master_guid=master;
                imported_plan->parts[n].resource.origin=value.object;imported_plan->parts[n].resource.runtime.master_guid=master;
            }
            auto imported_resource=imported_plan->parts.front().resource;imported_resource.group=imported_plan;
            const auto original=encode_session_bundle(imported);GroupAdapter moving_import;moving_import.expected_parts=count;moving_import.restored=true;
            const auto moved_import=prepare_ts1_object_move(imported,{group_sale_request(imported,0,&actual_avatar),{224,224,1,2}},imported_resource,moving_import.factory());
            prepared(moved_import,ObjectTradeTransactionStatus::prepared_success,"heterogeneous imported group movement");
            require(!moved_import.candidate->object_groups&&moved_import.candidate->object_trade->groups.front().members[0].object.master_guid!=
                moved_import.candidate->object_trade->groups.front().members[1].object.master_guid,
                "imported heterogeneous group keeps individual masters and never writes invented OGRP GUID0");
            GroupAdapter selling_import;selling_import.expected_parts=count;selling_import.restored=true;
            const auto sold_import=prepare_ts1_object_sale(imported,group_sale_request(imported,0,&actual_avatar),imported_resource,selling_import.factory());
            prepared(sold_import,ObjectTradeTransactionStatus::prepared_success,"heterogeneous imported static group sale");
            require(sold_import.candidate->removed_original_objects.size()==count&&!sold_import.candidate->object_groups&&
                encode_session_bundle(imported)==original,"imported sale retires every exact original generation without modifying its source");
            auto dynamic=live;auto dynamic_plan=std::make_shared<ObjectTradeGroupResources>(*selected.group);
            for(auto& part:dynamic_plan->parts)part.resource.runtime.entry_points.entries[8].action=4099;
            for(auto& entity:dynamic.world.entities)if(entity.kind==EntityKind::object)entity.entry_points->at(8).action=4099;
            auto dynamic_resource=dynamic_plan->parts.front().resource;dynamic_resource.group=dynamic_plan;
            const auto dynamic_before=encode_session_bundle(dynamic);GroupAdapter dynamic_delete;dynamic_delete.expected_parts=count;const auto unsupported=prepare_ts1_object_sale(dynamic,group_sale_request(dynamic,0,&actual_avatar),dynamic_resource,dynamic_delete.factory());
            require(!unsupported.candidate&&unsupported.status==ObjectTradeTransactionStatus::unsupported&&encode_session_bundle(dynamic)==dynamic_before,
                "dynamic selected-only deletion is not falsely replaced by static whole-group cleanup");
        }
        GroupAdapter denied;denied.expected_parts=count;denied.blocked=true;
        const auto refused=prepare_ts1_object_purchase(initial,request,resource,denied.factory());
        prepared(refused,ObjectTradeTransactionStatus::prepared_authored_refusal,"late-member authored purchase refusal");
        require(refused.candidate->host.family_budget==1000&&refused.candidate->object_trade->groups.empty()&&
            refused.candidate->object_threads.empty()&&refused.candidate->object_history->retired_created_objects.size()==count&&
            denied.place_prices.size()==count,"authored late-member refusal still runs all EP11 and deletes/refunds only once");
        GroupAdapter failed;failed.expected_parts=count;failed.technical=true;
        const auto technical=prepare_ts1_object_purchase(initial,request,resource,failed.factory());
        require(!technical.candidate&&technical.archive.empty()&&encode_session_bundle(initial)==initial_bytes,"late event-registration failure publishes no partial group");
        auto stale=std::make_shared<ObjectTradeGroupResources>(*selected.group);stale->parts.back().resource.origin.generation++;
        auto forged=selected;forged.group=stale;GroupAdapter rejecting;rejecting.expected_parts=count;
        require(!prepare_ts1_object_sale(live,group_sale_request(live,0,&actual_avatar),forged,rejecting.factory()).candidate,"stale later member resource cannot retire any group part");
    }
}
void actorless_multipart_trade_cases(){
    World world;require(world.create_runtime_object(1,0x9999,{},{})&&world.set_lockout_count_clock(0),"frozen lot baseline has an ordinary object and no resident");
    ActorlessObjectTradeState state;state.resources.resource_fingerprint.fill(0x44);state.resources.language_code=1;
    state.world=world.snapshot();state.relationships=RelationshipState{}.snapshot();state.history.complete=true;
    state.globals=RuntimeGlobals::from_legacy({}).snapshot();state.globals.words[32]=1;state.trade.version=2;
    state.scheduler=EntityVmScheduler(16).snapshot();state.random_seed=0x1122334455667788ULL;
    Ts1Clock clock;require(clock.initialize(9,4,12,7,2020),"frozen source clock");state.clock=clock.snapshot();
    const auto before=encode_actorless_object_trade_state(state);const auto resource=multipart_resource(3);
    ObjectPurchaseRequest request{2,{ObjectTradeCatalogState::present,resource.origin.guid,100},{160,160,1,0},{2,3,4}};
    GroupAdapter buying;buying.expected_parts=3;buying.actorless_source=true;
    const auto bought=prepare_ts1_actorless_object_purchase(state,request,resource,buying.factory());
    require(bought.status==ObjectTradeTransactionStatus::prepared_success&&bought.candidate,
        ("object-only multipart purchase: "+bought.diagnostic).c_str());
    const auto reopened=decode_actorless_object_trade_state(bought.archive,&state.resources);
    require(reopened.object_groups&&reopened.object_groups->groups.front().members.size()==3&&reopened.object_threads.size()==3&&
        reopened.trade.groups.front().initial_price==100&&encode_actorless_object_trade_state(reopened)==bought.archive,
        "object-only purchase persists physical group, shared price, every actual VM and native history together");
    auto components=actorless_object_trade_components(reopened);const auto selected=existing_multipart_resource(resource,components);
    GroupAdapter moving;moving.expected_parts=3;moving.actorless_source=true;
    const auto moved=prepare_ts1_actorless_object_move(reopened,{group_sale_request(components),{192,192,1,2}},selected,moving.factory());
    require(moved.status==ObjectTradeTransactionStatus::prepared_success&&moved.candidate,
        ("object-only multipart move: "+moved.diagnostic).c_str());
    const auto moved_reopened=decode_actorless_object_trade_state(moved.archive,&state.resources);
    components=actorless_object_trade_components(moved_reopened);const auto after_move=existing_multipart_resource(resource,components);
    GroupAdapter selling;selling.expected_parts=3;selling.actorless_source=true;selling.expected_sale_price=90;selling.deleting=true;
    const auto sold=prepare_ts1_actorless_object_sale(moved_reopened,group_sale_request(components),after_move,selling.factory());
    require(sold.status==ObjectTradeTransactionStatus::prepared_success&&sold.candidate,
        ("object-only multipart sale: "+sold.diagnostic).c_str());
    const auto final=decode_actorless_object_trade_state(sold.archive,&state.resources);const auto carrier=actorless_object_trade_components(final);
    require(final.world.entities.size()==1&&final.object_threads.empty()&&final.scheduler.entities.empty()&&
        final.trade.groups.empty()&&final.object_groups&&final.object_groups->groups.empty()&&final.history.retired_created_objects.size()==3&&
        !carrier.host.family_budget&&!carrier.host.resident_guid&&!carrier.host.runtime_avatar_id&&carrier.runtime_entities.empty(),
        "object-only sale retires the complete group without creating a budget, household, resident or avatar execution");
    require(encode_actorless_object_trade_state(state)==before,"object-only multipart command chain never mutates source input");
}
void selected_complete_program_cases(){
    const auto r=with_main(RoutineScope::private_object);Adapter buying;
    const auto bought=prepare_ts1_object_purchase(source(),buy_request(),r,buying.factory());
    prepared(bought,ObjectTradeTransactionStatus::prepared_success,"cross-owner source purchase");
    RestoredMainSession running(*bought.candidate,r);running.tick();auto sleeping=running.checkpoint();
    auto& thread=sleeping.object_threads.front();const CodeOwner foreign=0x80005678U;
    auto extra=thread.resource_owners.front();extra.owner=foreign;extra.identity=SessionCodeOwnerIdentity::process;
    thread.resource_owners.push_back(extra);sleeping.next_code_owner=foreign+1;
    // Resource banks may be installed under a process owner independently of
    // the selected entity identity. Retain the real sleep job/ticket/registers.
    for(auto& frame:thread.vm.stack)frame.context.code_owner=foreign;
    if(thread.vm.pending){thread.vm.pending->context.code_owner=foreign;thread.vm.pending->instruction.code_owner=foreign;}
    thread.vm.temp_xl[0]=123456;
    const auto expected=thread.vm;const auto source_bytes=encode_session_bundle(sleeping);
    auto private_bank=r.runtime.private_routines;private_bank.insert(r.linked_routines.begin(),r.linked_routines.end());
    const auto bank=std::make_shared<const VmProgram>(private_bank);
    ObjectTradeSelectedPrograms exact{std::make_shared<const VmProgram>(r.global_routines),
        {{thread.primary_owner,bank},{foreign,bank}}};
    // Establish all source invariants before exercising transaction rejection:
    // canonical manifest order/process allocator, same exact resource payload,
    // coherent scheduler and a strictly restorable foreign-owner pending Sleep.
    require(thread.resource_owners.front().owner<foreign&&sleeping.next_code_owner>foreign&&
        extra.resource_guid==thread.guid&&extra.routine_fingerprint==session_loaded_bank_fingerprint(bank->routines()),
        "cross-owner source manifest and allocator are coherent");
    World verified_world;require(verified_world.restore_snapshot(sleeping.world),"cross-owner source World restore");
    Sleep verified_sleep(verified_world);Vm verified(exact.globals,ShareVmProgramTag{},verified_world.vm_memory(),{},
        verified_sleep.primitive(),exact.owners,ShareVmOwnerProgramsTag{});
    require(verified.restore_snapshot(expected)&&verified_sleep.bind(verified)&&verified_sleep.restore_snapshot(thread.sleep)&&
        encode_vm_snapshot(verified.snapshot())==encode_vm_snapshot(expected),"strict cross-owner VM/Sleep source restores before transaction matrix");
    for(bool sale:{false,true}){
        const auto command=[&](Adapter& a){return sale?prepare_ts1_object_sale(sleeping,sell_request(sleeping),r,a.factory()):
            prepare_ts1_object_move(sleeping,{sell_request(sleeping),{48,48,1,6}},r,a.factory());};
        for(unsigned invalid=0;invalid<5;++invalid){
            Adapter denied;
            if(invalid){denied.selected_programs=exact;
                if(invalid==1)denied.selected_programs->owners.erase(foreign);
                if(invalid==2)denied.selected_programs->owners.emplace(0x9876,bank);
                if(invalid==3)denied.selected_programs->owners[foreign]=std::make_shared<const VmProgram>(RoutineSet{});
                if(invalid==4){auto forged=r.global_routines;forged.emplace(routine_key(257),Bhav{0x8002,{{99,254,255,{}}},{},Bhav::Metadata{0,0,0,0}});
                    denied.selected_programs->globals=std::make_shared<const VmProgram>(std::move(forged));}
            }
            const auto rejected=command(denied);
            require(!rejected.candidate&&denied.events.empty(),"missing/extra/forged selected programs reject before lifecycle");
        }
        Adapter admitted;admitted.selected_programs=exact;admitted.expected_bound=expected;
        admitted.lifecycle_source_registers=true;admitted.sale_temp=222;admitted.sale_xl=123456;
        const auto result=command(admitted);prepared(result,ObjectTradeTransactionStatus::prepared_success,"complete selected immutable owner collection");
        if(!sale)require(encode_vm_snapshot(result.candidate->object_threads.front().vm)==encode_vm_snapshot(expected)&&
            result.candidate->object_threads.front().sleep.job&&
            result.candidate->object_threads.front().sleep.job->ticket==thread.sleep.job->ticket&&
            result.candidate->object_threads.front().sleep.job->deadline==thread.sleep.job->deadline,
            "move retains selected cross-owner execution registers and exact actual Sleep job");
        else require(result.candidate->object_threads.empty(),"cross-owner sleeping source retires through sale");
    }
    require(encode_session_bundle(sleeping)==source_bytes,"cross-owner command matrix leaves source bytes unchanged");
}
void selected_source_restore_policy_cases(){
    const auto r=with_main(RoutineScope::private_object);Adapter buying;
    const auto bought=prepare_ts1_object_purchase(source(),buy_request(),r,buying.factory());
    prepared(bought,ObjectTradeTransactionStatus::prepared_success,"selected import-policy source");
    for(unsigned missing=0;missing<2;++missing){
        auto imported=*bought.candidate;auto& thread=imported.object_threads.front();
        thread.vm.status=VmStatus::dormant;
        if(missing)thread.vm.stack.front().key=routine_key(7000);
        else thread.vm.stack.front().pc=999;
        for(auto& scheduled:imported.scheduler.entities)if(scheduled.id==thread.id)scheduled.status=VmStatus::dormant;
        const auto bytes=encode_session_bundle(imported);const auto vm_bytes=encode_vm_snapshot(thread.vm);
        for(bool sale:{false,true}){
            Adapter strict;
            const auto rejected=sale?prepare_ts1_object_sale(imported,sell_request(imported),r,strict.factory()):
                prepare_ts1_object_move(imported,{sell_request(imported),{48,48,1,6}},r,strict.factory());
            require(!rejected.candidate&&rejected.archive.empty()&&strict.events.empty(),
                "selected missing routine/PC is rejected by default before any authored command checks");
            Adapter admitted;admitted.saved_policy=VmSavedRoutinePolicy::defer_missing_or_pc_fault;
            const auto result=sale?prepare_ts1_object_sale(imported,sell_request(imported),r,admitted.factory()):
                prepare_ts1_object_move(imported,{sell_request(imported),{48,48,1,6}},r,admitted.factory());
            prepared(result,ObjectTradeTransactionStatus::prepared_success,"authenticated selected import policy");
            require(encode_session_bundle(imported)==bytes,"selected deferred command leaves source file unchanged");
            if(sale)require(result.candidate->object_threads.empty()&&result.candidate->host.family_budget==1007,
                "selected deferred Main is disposed without running before real EP12/refund");
            else require(result.candidate->object_threads.size()==1&&encode_vm_snapshot(result.candidate->object_threads.front().vm)==vm_bytes&&
                result.candidate->host.family_budget==900,
                "selected move preserves invalid imported frame bytes and runs only authenticated placement checks");
        }
    }
}
void candidate_global_cases(){
    auto s=source();RuntimeGlobals globals(std::vector<std::int16_t>(32,0),Ts1GlobalActivation::saved_new,7);
    require(globals.write(9,10)&&globals.write(3,1),"known source global bank");s.runtime_globals=globals.snapshot();
    Ts1Clock clock;require(clock.initialize(13,2,5,6,1997),"known source simulation clock");
    s.simulation=SessionSimulationState{clock.snapshot(),{{1,{}}}};
    s.ui_state=SessionUiState{1,s.world.entities.front().generation,1};
    const auto original=encode_session_bundle(s);Adapter adapter;adapter.test_globals=true;
    const auto bought=prepare_ts1_object_purchase(s,buy_request(),resource(),adapter.factory());
    prepared(bought,ObjectTradeTransactionStatus::prepared_success,"candidate globals and read-only clock");
    require(adapter.saw_globals&&adapter.saw_clock&&bought.candidate->runtime_globals->words[9]==12&&
        bought.candidate->runtime_globals->words[0]==999&&bought.candidate->runtime_globals->words[3]==0&&
        bought.candidate->simulation->clock.ticks==s.simulation->clock.ticks&&
        bought.candidate->simulation->clock.hour==13&&bought.candidate->ui_state->active_actor==1&&
        encode_session_bundle(s)==original,"candidate globals persist; clock/UI/source remain distinct and unchanged");
    const auto sold=prepare_ts1_object_sale(*bought.candidate,sell_request(*bought.candidate),resource(),adapter.factory());
    prepared(sold,ObjectTradeTransactionStatus::prepared_success,"EP12 global effects persist");
    require(sold.candidate->runtime_globals->words[9]==13,"sale persists actual candidate global writes");
    Adapter refused;refused.test_globals=true;refused.blocked=true;
    const auto failure=prepare_ts1_object_purchase(s,buy_request(),resource(),refused.factory());
    prepared(failure,ObjectTradeTransactionStatus::prepared_authored_refusal,"authored refusal preserves global effects");
    require(failure.candidate->runtime_globals->words[9]==12,"Init and EP11 global effects survive authored refusal");
    Adapter old;const auto legacy=prepare_ts1_object_purchase(source(),buy_request(),resource(),old.factory());
    prepared(legacy,ObjectTradeTransactionStatus::prepared_success,"unknown legacy global/clock capability");
    require(!old.saw_globals&&!old.saw_clock&&!legacy.candidate->runtime_globals&&!legacy.candidate->simulation,
        "missing global/time proof remains absent, never fabricated from defaults");
}
void permission_cases(){
    auto s=source();s.build_buy_permission=BuildBuyPermissionSnapshot{false};
    const auto before=encode_session_bundle(s);const auto r=resource();Adapter a;
    const auto purchase=prepare_ts1_object_purchase(s,buy_request(),r,a.factory());
    const auto sale=prepare_ts1_object_sale(s,{},r,a.factory());
    const auto move=prepare_ts1_object_move(s,{},r,a.factory());
    for(const auto* result:{&purchase,&sale,&move})require(result->status==ObjectTradeTransactionStatus::unsupported&&
        !result->candidate&&result->archive.empty()&&result->diagnostic=="Build/Buy is disabled by the current lot simulation",
        "all object transaction kinds reject captured false permission before admission/effects");
    require(a.events.empty()&&a.observed_budget.empty()&&encode_session_bundle(s)==before,
        "disabled transactions invoke no lifecycle callback and retain source bytes/funds");
    s.build_buy_permission=BuildBuyPermissionSnapshot{true};
    const auto permitted=prepare_ts1_object_purchase(s,buy_request(),r,a.factory());
    prepared(permitted,ObjectTradeTransactionStatus::prepared_success,"explicit enabled permission");
    require(permitted.candidate->build_buy_permission&&permitted.candidate->build_buy_permission->enabled,
        "transaction reconstruction preserves the captured permission section");
}
void actorless_cases(){
    const auto original=source();
    ActorlessObjectTradeState state;state.resources=original.resources;state.world=original.world;
    state.relationships=original.relationships;state.history=*original.object_history;
    state.globals=RuntimeGlobals::from_legacy({}).snapshot();state.globals.words[32]=1;
    state.random_seed=0x123456789abcdef0ULL;state.scheduler=EntityVmScheduler(4).snapshot();
    const auto before=encode_actorless_object_trade_state(state);
    auto r=resource();r.runtime.entry_points.entries[1].action=4096;
    Adapter adapter;adapter.no_family=true;adapter.expected_main=routine_key(4096);
    const auto bought=prepare_ts1_actorless_object_purchase(state,buy_request(),r,adapter.factory());
    require(bought.status==ObjectTradeTransactionStatus::prepared_success&&bought.candidate&& !bought.archive.empty(),
        ("actorless real lifecycle purchase: "+bought.diagnostic).c_str());
    require(bought.candidate->object_threads.size()==1&&bought.candidate->object_threads[0].vm.steps==0&&
        bought.candidate->object_threads[0].vm.stack.size()==1&&bought.candidate->object_threads[0].vm.stack[0].pc==0,
        "actorless Init stages Main but never executes simulation");
    require(encode_actorless_object_trade_state(decode_actorless_object_trade_state(bought.archive))==bought.archive&&
        encode_actorless_object_trade_state(state)==before,"actorless purchase roundtrip leaves source immutable");
    auto components=actorless_object_trade_components(*bought.candidate);
    auto admission=sell_request(components);admission.move_objects=false;
    Vm actual_frozen_avatar(RoutineSet{});require(actual_frozen_avatar.restore_snapshot(original.runtime_entities.front().vm),
        "restore actual fixture source avatar Thread, not guessed empty authority");
    admission.avatar_threads={{{1,state.world.entities.front().generation},&actual_frozen_avatar}};
    admission.avatar_threads_complete=true;
    admission.groups={{true,{{1,state.world.entities.front().generation}}},{true,{{2,bought.candidate->created_objects.front().generation}}}};
    admission.buildable=ObjectTradeBuildableArea{8,8,2,0,0,8,8,{}};admission.selected_disabled=0;
    Adapter moving;moving.no_family=true;
    const auto moved=prepare_ts1_actorless_object_move(*bought.candidate,{admission,{48,48,1,7}},r,moving.factory());
    require(moved.status==ObjectTradeTransactionStatus::prepared_success&&moved.candidate,
        ("actorless move uses real position lifecycle: "+moved.diagnostic).c_str());
    Adapter selling;selling.no_family=true;
    const auto sold=prepare_ts1_actorless_object_sale(*moved.candidate,admission,r,selling.factory());
    require(sold.status==ObjectTradeTransactionStatus::prepared_success&&sold.candidate,
        ("actorless sale uses real delete lifecycle: "+sold.diagnostic).c_str());
    require(sold.candidate->world.entities.size()==state.world.entities.size()&&sold.candidate->object_threads.empty()&&
        sold.candidate->history.reserved_entity_ids==std::vector<std::int16_t>{2}&&sold.candidate->trade.objects.empty(),
        "actorless sale retains identity history without family money or running Main");
    const auto scratch=actorless_object_trade_components(*sold.candidate);
    require(!scratch.host.family_budget&&!scratch.host.resident_guid&&!scratch.host.runtime_avatar_id&&
        encode_actorless_object_trade_state(decode_actorless_object_trade_state(sold.archive))==sold.archive,
        "actorless sale remains actorless after fresh decode");
    Adapter refusal;refusal.no_family=true;refusal.blocked=true;
    const auto refused=prepare_ts1_actorless_object_purchase(state,buy_request(),r,refusal.factory());
    require(refused.status==ObjectTradeTransactionStatus::prepared_authored_refusal&&refused.candidate&&
        refused.candidate->history.reserved_entity_ids==std::vector<std::int16_t>{2},
        "actorless authored placement refusal preserves Init effects and burned identity");
}

void shared_value_registry_cases(){
    const auto s=source();const auto unchanged=encode_session_bundle(s);const auto r=resource();
    for(const auto value:std::array<std::int16_t,3>{0,43,100}){
        Adapter a;a.init_current_value=value;a.ep11_current_value=static_cast<std::int16_t>(value+1);
        const auto bought=prepare_ts1_object_purchase(s,buy_request(),r,a.factory());
        prepared(bought,ObjectTradeTransactionStatus::prepared_success,"Init/EP11 shared CurrentValue purchase");
        const auto& ledger=*bought.candidate->object_trade;const auto& group=ledger.groups.front();
        require(ledger.version==2&&ledger.objects.empty()&&group.initial_price==100&&
            group.price_origin.kind==SessionObjectPriceSource::purchase&&!group.price_origin.writer&&
            group.members.front().disabled==0&&bought.candidate->world.entities.back().object_words.at(41)==value+1,
            "successful UI purchase overrides authored Init/EP11 shared price, preserves raw word and runtime Disabled");
        Adapter sale;const auto sold=prepare_ts1_object_sale(*bought.candidate,sell_request(*bought.candidate),r,sale.factory());
        prepared(sold,ObjectTradeTransactionStatus::prepared_success,"sale uses final successful purchase price");
        require(sold.candidate->host.family_budget==1007&&sold.candidate->object_trade->groups.empty(),
            "sale refunds final purchase price once after EP12 and retires its registry member");
        require(encode_session_bundle(s)==unchanged,"value writes affect only the private candidate World and ledger");
        // An authored placement refusal executes EP11 but never performs the
        // successful-buy override; Init/EP11 side effects on other state survive.
        Adapter blocked;blocked.init_current_value=value;blocked.ep11_current_value=static_cast<std::int16_t>(value+1);blocked.blocked=true;
        const auto refused=prepare_ts1_object_purchase(s,buy_request(),r,blocked.factory());
        prepared(refused,ObjectTradeTransactionStatus::prepared_authored_refusal,"authored failure after price writes");
        require(refused.candidate->object_trade->groups.empty()&&refused.candidate->host.family_budget==1000,
            "authored failure deletes the group and refunds catalog amount without publishing a successful price");
        a.technical_failure=true;
        const auto failed=prepare_ts1_object_purchase(s,buy_request(),r,a.factory());
        require(!failed.candidate&&failed.archive.empty()&&encode_session_bundle(s)==unchanged,
            "failure after Init shared-value mutation publishes neither World nor ledger");
    }
    Adapter initial;const auto bought=prepare_ts1_object_purchase(s,buy_request(),r,initial.factory());
    prepared(bought,ObjectTradeTransactionStatus::prepared_success,"legacy upgrade fixture purchase");
    auto legacy=*bought.candidate;const auto& old_group=legacy.object_trade->groups.front();
    SessionObjectTradeLedger v1;v1.objects.push_back({old_group.members.front().object,*old_group.initial_price,old_group.members.front().wear});
    legacy.object_trade=v1;const auto legacy_bytes=encode_session_bundle(legacy);
    require(encode_session_bundle(decode_session_bundle(legacy_bytes))==legacy_bytes,"v1 codec roundtrip is preserved independently of transaction migration");
    Adapter legacy_move;const auto moved=prepare_ts1_object_move(legacy,{sell_request(legacy),{48,48,1,0}},r,legacy_move.factory());
    prepared(moved,ObjectTradeTransactionStatus::prepared_success,"legacy migration does not invent runtime Disabled");
    require(!moved.candidate->object_trade->groups.front().members.front().disabled,
        "legacy price provenance is not fresh-import Disabled provenance");
    Adapter selling;const auto sold=prepare_ts1_object_sale(legacy,sell_request(legacy),r,selling.factory());
    prepared(sold,ObjectTradeTransactionStatus::prepared_success,"explicit legacy singleton migration in transaction");
    require(sold.candidate->object_trade->version==2&&sold.candidate->object_trade->objects.empty()&&
        sold.candidate->object_trade->groups.empty()&&encode_session_bundle(legacy)==legacy_bytes,
        "transaction upgrades its private ledger while the legacy input bytes remain unchanged");
    auto second=buy_request();second.new_id=3;Adapter another;
    const auto two=prepare_ts1_object_purchase(*bought.candidate,second,r,another.factory());
    prepared(two,ObjectTradeTransactionStatus::prepared_success,"two singleton grouping guard fixture");
    auto grouped=*two.candidate;auto& first=grouped.object_trade->groups[0];const auto other=grouped.object_trade->groups[1].members[0];
    first.members.push_back(other);first.multitile=true;grouped.object_trade->groups.erase(grouped.object_trade->groups.begin()+1);
    const auto grouped_bytes=encode_session_bundle(grouped);Adapter reject;
    const auto refused=prepare_ts1_object_sale(grouped,sell_request(grouped),r,reject.factory());
    require(refused.status==ObjectTradeTransactionStatus::unsupported&&!refused.candidate&&
        refused.diagnostic.find("singleton")!=std::string::npos&&encode_session_bundle(grouped)==grouped_bytes,
        "known group price does not pretend multipart sale lifecycle exists");
}
}
int main(){try{shared_value_registry_cases();actorless_cases();permission_cases();success_and_sale();supported_leaf_sale_cases();reused_creation_history_cases();move_rotation_cases();refusals_and_unknown();main_scheduling_cases();native_purchase_constructor_cases();multipart_trade_matrix();actorless_multipart_trade_cases();selected_complete_program_cases();selected_source_restore_policy_cases();candidate_global_cases();std::cout<<"object trade transaction fixtures passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
