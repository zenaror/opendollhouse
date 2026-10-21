#include "odh/entity_next.hpp"
#include <iostream>
#include <memory>

namespace {
int checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
constexpr std::uint32_t guid=0xF8A12345;
void add(odh::World& w,std::int16_t id,std::uint32_t type=guid,odh::EntityKind kind=odh::EntityKind::object,
         std::optional<std::uint32_t> master={}){
    require(w.create(id,kind)&&w.define_type(id,type,master),"create synthetic known entity");
}
odh::Instruction next(unsigned scope=10,unsigned index=0,unsigned flags=132,std::uint32_t type=guid){
    return {31,1,1,{static_cast<std::uint8_t>(type),static_cast<std::uint8_t>(type>>8),
        static_cast<std::uint8_t>(type>>16),static_cast<std::uint8_t>(type>>24),
        static_cast<std::uint8_t>(flags),static_cast<std::uint8_t>(scope),255,static_cast<std::uint8_t>(index)}};
}
odh::VmFrame frame(std::int16_t cursor=11){
    return {odh::routine_key(4096),0,{101,202,cursor,37},{0,cursor},{0,cursor}};
}
odh::PrimitiveOutcome dispatch(const odh::VmPrimitive& p,const odh::VmFrame& f,const odh::Instruction& i,
    const std::array<std::int16_t,20>& temps={}){return p(f,i,temps,{nullptr,17});}
bool no_output(const odh::PrimitiveOutcome& r){return !r.write&&!r.temp_write&&!r.stack_object_write&&!r.call;}
void live_query(){
    odh::World w;
    add(w,19);add(w,11);add(w,15,guid,odh::EntityKind::avatar);add(w,12,8,odh::EntityKind::object,guid);
    require(w.create(13,odh::EntityKind::object),"unknown definition entity");
    require(w.declare_object_word(15,64,1),"avatar arbitrary object word does not exclude enumeration");
    for(const auto& pair:{std::pair<std::int16_t,std::int16_t>{-32768,11},{-1,11},{0,11},
                         {11,15},{12,15},{14,15},{15,19},{18,19}})
        require(w.next_entity_of_guid(pair.first,guid)==pair.second,"ascending exact GUID and signed missing cursors");
    require(!w.next_entity_of_guid(19,guid)&&!w.next_entity_of_guid(32767,guid),"end never wraps");
    require(!w.next_entity_of_guid(0,0)&&w.next_entity_of_guid(0,8)==12,"unknown definition and master alias omitted");
    require(w.remove(15)&&w.next_entity_of_guid(11,guid)==19,"removal affects next immediately");
    add(w,15,8);require(w.next_entity_of_guid(11,guid)==19,"ID reuse with different GUID is skipped");
    require(w.remove(15),"remove reused ID");add(w,15);
    require(w.next_entity_of_guid(11,guid)==15,"same ID new definition immediately visible");
    require(w.define_type(13,guid)&&w.next_entity_of_guid(11,guid)==13,"newly known definition immediately visible");
    const auto before=w.word_count();const auto generation=w.find(11)->generation;
    for(int n=0;n<100;++n)require(w.next_entity_of_guid(0,guid)==11,"repeat live query stable");
    require(w.word_count()==before&&w.find(11)->generation==generation&&w.find(11)->definition->guid==guid,
        "query does not mutate world words or generation");
    odh::World empty;require(!empty.next_entity_of_guid(-32768,guid),"empty world ends");
    add(empty,2,0);require(empty.next_entity_of_guid(0,0)==2,"explicit zero GUID is known and searchable");
    odh::World full({32767,0,0});
    for(int id=1;id<=32767;++id){
        if(!full.create(static_cast<std::int16_t>(id),odh::EntityKind::object)||
           !full.define_type(static_cast<std::int16_t>(id),id==32767?guid:0))throw std::runtime_error("full signed entity ID range setup");
    }
    require(full.next_entity_of_guid(-32768,guid)==32767&&full.next_entity_of_guid(32766,guid)==32767&&
        !full.next_entity_of_guid(32767,guid),"full world finds signed maximum without overflow/wrap");
    require(!full.create(0,odh::EntityKind::object)&&!full.create(-1,odh::EntityKind::object),"World object identities stay positive");
}
void same_tile_query(){
    odh::World w;
    for(const auto [id,kind]:{std::pair<std::int16_t,odh::EntityKind>{10,odh::EntityKind::object},
        {15,odh::EntityKind::avatar},{20,odh::EntityKind::object},{30,odh::EntityKind::object},
        {40,odh::EntityKind::object}})require(w.create(id,kind),"create same-tile entity");
    require(w.create(25,odh::EntityKind::object),"create unpositioned entity");
    require(w.set_position(10,{16,32,1,0})&&w.set_position(15,{31,47,1,0})&&
        w.set_position(20,{17,33,1,0})&&w.set_position(30,{32,32,1,0})&&
        w.set_position(40,{16,32,2,0}),"assign same-tile positions");
    require(w.next_entity_on_same_tile(10)==15&&w.next_entity_on_same_tile(15)==20&&
        w.next_entity_on_same_tile(20)==10,"same-tile query ascends IDs then wraps to first");
    require(w.next_entity_on_same_tile(30)==30&&w.next_entity_on_same_tile(40)==40,
        "same-tile query keeps tile and level identity");
    require(!w.next_entity_on_same_tile(11),"same-tile cursor must identify a positioned entity");
    const auto p=odh::entity_next_primitive(w);
    auto r=dispatch(p,frame(10),next(10,0,136));
    require(r.disposition==odh::PrimitiveDisposition::goto_true&&r.stack_object_write&&
        r.stack_object_write->value==15,"opcode31 same-tile mode writes next StackObjectID");
    r=dispatch(p,frame(20),next(10,0,136));
    require(r.disposition==odh::PrimitiveDisposition::goto_true&&r.stack_object_write&&
        r.stack_object_write->value==10,"opcode31 same-tile mode wraps to first matching entity");
    r=dispatch(p,frame(25),next(10,0,136));
    require(r.disposition==odh::PrimitiveDisposition::unsupported&&no_output(r),
        "opcode31 same-tile mode rejects an unpositioned cursor");
    auto unsupported=next(10,0,137);
    r=dispatch(p,frame(10),unsupported);
    require(r.disposition==odh::PrimitiveDisposition::unsupported&&no_output(r),
        "adjacent-tile mode remains unsupported");
}
void selectors(){
    odh::World w;add(w,11);add(w,15);add(w,19);
    const auto f=frame();std::array<std::int16_t,20> temps{};temps[1]=11;
    unsigned seen=0;
    const auto p=odh::entity_next_primitive(w,{},[&](const odh::EntityNext& r){
        require(r.guid==guid&&r.current==11&&r.result==15&&r.success&&r.index==1&&
            r.context.caller==101&&r.context.callee==202&&r.context.stack_object==11&&r.context.code_owner==37,
            "observer captures original owner/context and high byte GUID");++seen;
    });
    for(unsigned scope:{8U,9U,10U,25U}){
        auto r=dispatch(p,f,next(scope,1),temps);require(r.disposition==odh::PrimitiveDisposition::goto_true,"writable selector true");
        if(scope==8)require(r.temp_write&&r.temp_write->index==1&&r.temp_write->value==15&&!r.write&&!r.stack_object_write,"temp output staged");
        if(scope==9||scope==25)require(r.write&&r.write->index==1&&r.write->value==15&&
            r.write->bank==(scope==9?odh::VmFrameBank::arguments:odh::VmFrameBank::locals)&&!r.temp_write&&!r.stack_object_write,"frame output staged");
        if(scope==10)require(r.stack_object_write&&r.stack_object_write->value==15&&!r.temp_write&&!r.write,"stack identity staged");
    }
    require(seen==4&&f.context.stack_object==11&&f.arguments[1]==11&&f.locals[1]==11&&temps[1]==11,"dispatch retains native banks before VM publication");
    const auto legacy=dispatch(odh::entity_next_primitive(w),f,next(8,255,4),temps);
    require(legacy.stack_object_write&&legacy.stack_object_write->value==15,"legacy operands force stack scope/index");
    const auto ignored_index=dispatch(odh::entity_next_primitive(w),f,next(10,255),temps);
    require(ignored_index.stack_object_write&&ignored_index.stack_object_write->value==15,"stack scope ignores encoded data byte");
    const auto literal=dispatch(odh::entity_next_primitive(w),f,next(7,11),temps);
    require(literal.disposition==odh::PrimitiveDisposition::unsupported&&no_output(literal)&&!literal.diagnostic.empty(),"literal success cannot write");
    for(unsigned scope:{8U,9U,25U,30U,31U}){
        const auto r=dispatch(odh::entity_next_primitive(w),f,next(scope,255),temps);
        require(r.disposition==odh::PrimitiveDisposition::unsupported&&no_output(r),"unreadable selector rejects");
    }
    auto wrong=next();wrong.opcode=30;
    for(const auto i:{wrong,next(10,0,133),next(10,0,131),next(10,0,255)}){
        const auto r=dispatch(odh::entity_next_primitive(w),f,i);
        require(r.disposition==odh::PrimitiveDisposition::unsupported&&r.diagnostic.empty()&&no_output(r),"unhandled opcodes/modes leave dispatcher unsupported");
    }
}
void external_and_false(){
    odh::World w;add(w,11);add(w,15);
    auto f=frame();std::array<std::int16_t,20> temps{};temps[1]=3;
    for(unsigned scope:{19U,30U,31U}){
        auto state=std::make_shared<std::int16_t>(11);unsigned reads=0,writes=0,seen=0;
        const auto expected_scope=scope==30?18:19;const auto expected_index=scope==19?1:3;
        odh::VmMemory memory;
        memory.read=[state,&reads,expected_scope,expected_index](const auto& frame,auto s,auto index)->std::optional<std::int16_t>{
            require(frame.context.code_owner==37&&frame.context.stack_object==11&&s==expected_scope&&index==expected_index,"memory read original context and resolved address");++reads;return *state;
        };
        memory.write=[state,&writes,expected_scope,expected_index](const auto& frame,auto s,auto index,auto value){
            require(frame.context.caller==101&&frame.context.callee==202&&s==expected_scope&&index==expected_index&&value==15,"memory write resolved address and value");++writes;*state=value;return true;
        };
        auto p=odh::entity_next_primitive(w,memory,[&](const auto& r){require(r.scope==scope&&r.index==1&&r.success,"observer retains encoded external target");++seen;});
        memory={}; // Callable copies must survive destruction of the supplied wrapper.
        const auto r=dispatch(p,f,next(scope,1),temps);
        require(r.disposition==odh::PrimitiveDisposition::goto_true&&no_output(r)&&*state==15&&reads==1&&writes==1&&seen==1,"owned callbacks external continuation");
    }
    unsigned writes=0,seen=0;std::int16_t state=11;
    odh::VmMemory denied{[&](const auto&,auto,auto)->std::optional<std::int16_t>{return state;},
        [&](const auto&,auto,auto,auto){++writes;return false;}};
    auto r=dispatch(odh::entity_next_primitive(w,denied,[&](const auto&){++seen;}),f,next(19,1));
    require(r.disposition==odh::PrimitiveDisposition::unsupported&&no_output(r)&&!r.diagnostic.empty()&&state==11&&writes==1&&seen==0,"rejected unchanged backend write leaves no VM output or observer");
    denied.write={};r=dispatch(odh::entity_next_primitive(w,denied),f,next(19,1));
    require(r.disposition==odh::PrimitiveDisposition::unsupported&&no_output(r),"missing external writer rejects found result");
    denied.read={};r=dispatch(odh::entity_next_primitive(w,denied),f,next(19,1));
    require(r.disposition==odh::PrimitiveDisposition::unsupported&&no_output(r),"missing external reader rejects");
    denied.read=[](const auto&,auto,auto)->std::optional<std::int16_t>{return {};};
    r=dispatch(odh::entity_next_primitive(w,denied),f,next(19,1));
    require(r.disposition==odh::PrimitiveDisposition::unsupported&&no_output(r),"unavailable backend word rejects");
    denied.read=[](const auto&,auto scope,auto index)->std::optional<std::int16_t>{
        require(scope==19&&index==255,"encoded target index is unsigned byte even above127");return 11;};
    denied.write=[](const auto&,auto scope,auto index,auto value){return scope==19&&index==255&&value==15;};
    r=dispatch(odh::entity_next_primitive(w,denied),f,next(19,255));
    require(r.disposition==odh::PrimitiveDisposition::goto_true,"high target byte reaches callback nonnegative");
    f=frame(15);temps[1]=15;seen=0;writes=0;
    denied.read=[](const auto&,auto,auto)->std::optional<std::int16_t>{return 15;};
    denied.write=[&](const auto&,auto,auto,auto){++writes;return false;};
    const auto p=odh::entity_next_primitive(w,denied,[&](const auto& record){
        require(record.current==15&&!record.result&&!record.success,"false observer has no invented result");++seen;
    });
    for(unsigned scope:{7U,8U,9U,10U,25U,19U}){
        const auto index=scope==7?15U:1U;r=dispatch(p,f,next(scope,index),temps);
        require(r.disposition==odh::PrimitiveDisposition::goto_false&&no_output(r),"missing candidate returns false before write validation");
    }
    require(seen==6&&writes==0&&f.context.stack_object==15&&temps[1]==15&&f.arguments[1]==15,"false preserves all targets and avoids write callback");
    odh::World unknown;r=dispatch(odh::entity_next_primitive(unknown),frame(),next(7,255));
    require(r.disposition==odh::PrimitiveDisposition::goto_false&&no_output(r),"literal no result does not validate output");
}
void actual_vm(){
    odh::World w;add(w,11);add(w,15);
    const auto key=odh::routine_key(4096);
    for(unsigned scope:{8U,9U,10U,25U}){
        odh::Bhav routine{0x8002,{next(scope,1),{31,254,255,next(scope,1).operands}},{},odh::Bhav::Metadata{0,2,2,0}};
        // Repeat selects the next item and then exhausts; both branch targets on
        // the first instruction are1, so the second instruction tests no wrap.
        odh::RoutineOwners owners{{37,{{key,routine}}}};
        odh::Vm vm({}, {}, {}, odh::entity_next_primitive(w),owners);
        require(vm.start(key,{101,202,11,37},{0,11})==odh::VmStatus::running,"VM enters owned routine");
        vm.temps()[1]=11;
        // Locals begin at zero; initialize local1 via a real Expression first.
        if(scope==25){
            odh::Bhav local{0x8002,{{2,1,255,{1,0,11,0,0,5,25,7}},next(scope,1),{31,254,255,next(scope,1).operands}},{},odh::Bhav::Metadata{0,2,2,0}};
            local.instructions[1].on_true=2;local.instructions[1].on_false=2;
            odh::Vm locals({}, {}, {}, odh::entity_next_primitive(w),{{37,{{key,local}}}});
            require(locals.start(key,{101,202,11,37},{0,11})==odh::VmStatus::running&&locals.run(2)==odh::VmStatus::budget_exhausted&&locals.stack()[0].locals[1]==15,"actual VM publishes local after expression initializes cursor");
            require(locals.run(1)==odh::VmStatus::returned_false,"local resume after exhaustion returns false");continue;
        }
        require(vm.run(1)==odh::VmStatus::budget_exhausted,"one step applies output before budget suspension");
        const auto& f=vm.stack()[0];
        require((scope==8?vm.temps()[1]:scope==9?f.arguments[1]:f.context.stack_object)==15&&
            f.context.caller==101&&f.context.callee==202&&f.context.code_owner==37,"actual VM publishes selected bank and preserves context");
        require(vm.run(1)==odh::VmStatus::returned_false,"resumed enumeration exhausts without wrap");
    }
}
void kinds(){
    odh::World w;
    require(w.create(3,odh::EntityKind::object)&&w.create(5,odh::EntityKind::avatar)&&
        w.create(7,odh::EntityKind::object)&&w.create(11,odh::EntityKind::avatar),"mixed untyped entities created");
    add(w,15,0xFFFFFFFF,odh::EntityKind::object);add(w,19,0x01234567,odh::EntityKind::avatar);
    const auto words=w.word_count();
    for(const auto& pair:{std::pair<std::int16_t,std::int16_t>{-32768,3},{-1,3},{0,3},{3,5},
        {4,5},{5,7},{7,11},{11,15},{15,19}})
        require(w.next_entity(pair.first)==pair.second,"any enumeration signed cursor and ordered untyped entities");
    require(w.next_entity(-32768,odh::EntityKind::avatar)==5&&w.next_entity(6,odh::EntityKind::avatar)==11&&
        w.next_entity(11,odh::EntityKind::avatar)==19&&!w.next_entity(19,odh::EntityKind::avatar),"avatar query skips objects and does not wrap");
    require(w.next_entity(-1,odh::EntityKind::object)==3&&w.next_entity(4,odh::EntityKind::object)==7&&
        w.next_entity(7,odh::EntityKind::object)==15&&!w.next_entity(15,odh::EntityKind::object),"object query skips avatars and does not wrap");
    require(!w.next_entity(19)&&!w.next_entity(32767)&&!w.next_entity(32767,odh::EntityKind::avatar)&&
        !w.next_entity_of_guid(0,guid),"end queries do not wrap and type query still excludes unknown definitions");
    const auto key=odh::routine_key(4096);
    for(unsigned mode:{0U,1U,2U}){
        const auto expected=mode==1?5:3;
        const auto second=mode==0?5:mode==1?11:7;
        unsigned seen=0;
        auto p=odh::entity_next_primitive(w,{},[&](const auto& record){
            require(record.mode==mode&&record.guid==0xFFFFFFFF&&record.success&&record.current==-9&&
                record.result==expected,"observer includes mode and unused raw GUID");++seen;
        });
        const auto r=dispatch(p,frame(-9),next(10,255,128+mode,0xFFFFFFFF));
        require(r.disposition==odh::PrimitiveDisposition::goto_true&&r.stack_object_write&&
            r.stack_object_write->value==expected&&seen==1,"modes include untyped entities and ignore GUID/local bytes");
        const auto legacy=dispatch(odh::entity_next_primitive(w),frame(-9),next(8,255,mode,0xDEADBEEF));
        require(legacy.stack_object_write&&legacy.stack_object_write->value==expected&&!legacy.temp_write,
            "kind modes legacy flags force stack despite target encoding");
        odh::Bhav routine{0x8002,{next(10,0,128+mode,0xDEADBEEF),next(10,0,128+mode,0xCAFEBABE)},
            {},odh::Bhav::Metadata{0,0,0,0}};
        routine.instructions[1].on_true=254;routine.instructions[1].on_false=255;
        odh::Vm vm({{key,routine}}, {}, {}, odh::entity_next_primitive(w));
        require(vm.start(key,{101,202,-9,0})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::budget_exhausted&&
            vm.stack()[0].context.stack_object==expected,"actual kind VM writes first selected entity");
        require(vm.run(1)==odh::VmStatus::returned_true,"actual kind VM resumes to second selected entity");
        // Last output is no longer inspectable after return; retain second
        // execution's live frame by replacing its terminal branch temporarily.
        routine.instructions[1].on_true=0;routine.instructions[1].on_false=0;
        odh::Vm continuation({{key,routine}}, {}, {}, odh::entity_next_primitive(w));
        require(continuation.start(key,{101,202,-9,0})==odh::VmStatus::running&&
            continuation.run(2)==odh::VmStatus::budget_exhausted&&continuation.stack()[0].context.stack_object==second,
            "second kind ID follows no-existing signed cursor and native VM staging");
        const auto end=dispatch(odh::entity_next_primitive(w),frame(19),next(10,0,128+mode));
        require(end.disposition==odh::PrimitiveDisposition::goto_false&&no_output(end),"each kind mode reaches false without wrap");
    }
    require(w.word_count()==words&&!w.find(3)->definition&&!w.find(5)->definition,
        "kind enumeration does not invent definitions or allocate world words");
    require(w.remove(5)&&w.next_entity(3,odh::EntityKind::avatar)==11,"live avatar removal reflected");
    require(w.create(5,odh::EntityKind::object)&&w.next_entity(3,odh::EntityKind::avatar)==11&&
        w.next_entity(3,odh::EntityKind::object)==5,"reused ID new kind reflected");
    require(w.remove(5)&&w.create(5,odh::EntityKind::avatar)&&w.next_entity(3,odh::EntityKind::avatar)==5,
        "ID reused as untyped avatar immediately participates");
}
}
int main(){try{live_query();same_tile_query();selectors();external_and_false();actual_vm();kinds();std::cout<<checks<<" entity-next checks passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<"entity-next failure: "<<e.what()<<'\n';return 1;}}
