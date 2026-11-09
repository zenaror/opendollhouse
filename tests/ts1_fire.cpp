#include "odh/ts1_fire.hpp"
#include "functional_selector_host.hpp"
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
namespace {
using namespace odh;
unsigned checks=0;
void require(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
Bhav routine(std::vector<Instruction> code){return {0x8002,std::move(code),{},Bhav::Metadata{0,4,4,0}};}
struct Fixture {
    World world;
    Ts1Clock clock;
    Ts1FireServices services;
    std::unique_ptr<Vm> vm;
    std::map<std::int16_t,std::unique_ptr<Vm>> other_avatar_threads;
    PrimitiveOutcome outcome;
    const VmFrame* active_source=nullptr;
    std::function<void(const VmFrame&)> before_burn;
    unsigned random_calls=0,create_calls=0,flag_calls=0;
    std::uint32_t draw=0;
    bool pool=false,refuse_constructor=false,normal_null_constructor=false;
    std::int16_t next_id=1000;
    std::vector<EntityPosition> created;
    std::vector<std::string> effects;
    std::map<std::int16_t,std::vector<WorldEntityReference>> groups;
    Fixture(){
        add(1,EntityKind::avatar,{8,8,1,0},101);
        add(2,EntityKind::object,{24,24,1,0},0);
        services.width=services.height=8;
        services.source_current=[] {return true;};
        services.random=[&](std::uint32_t limit)->std::optional<std::uint32_t>{
            require(limit==10000,"Burn uses exact source random bound");++random_calls;effects.push_back("random");return draw;};
        services.caller_in_pool=[&](WorldEntityReference caller)->std::optional<bool>{
            require(caller.id==1&&world.is_live_reference(caller),"pool query uses exact Caller, not target");effects.push_back("pool");return pool;};
        services.position=[&](WorldEntityReference ref)->std::optional<EntityPosition>{
            const auto* entity=world.resolve_reference(ref);if(!entity)return {};
            return entity->position.value_or(EntityPosition{-32768,-32768,1,0});};
        services.objects_at=[&](const EntityPosition& point)->std::optional<std::vector<WorldEntityReference>>{
            std::vector<WorldEntityReference> result;
            for(std::int16_t cursor=0;;){const auto next=world.next_entity(cursor);if(!next)break;cursor=*next;
                const auto* entity=world.find(cursor);if(entity->ui_ghost||!entity->position)continue;
                if(entity->position->x/16==point.x/16&&entity->position->y/16==point.y/16&&entity->position->level==point.level)
                    result.push_back({cursor,entity->generation});}
            return result;};
        services.group=[&](WorldEntityReference ref)->std::optional<std::vector<WorldEntityReference>>{
            if(!world.is_live_reference(ref))return {};const auto found=groups.find(ref.id);
            return found==groups.end()?std::vector<WorldEntityReference>{ref}:found->second;};
        services.in_use=[&](WorldEntityReference ref){
            return odh_host::functional_selector_in_use(world,ref,services.group,
                [&](WorldEntityReference owner)->const Vm*{
                    if(owner.id==1)return vm.get();
                    const auto found=other_avatar_threads.find(owner.id);
                    return found==other_avatar_threads.end()?nullptr:found->second.get();});};
        services.set_flags=[&](WorldEntityReference ref,std::int16_t value,std::string&){
            ++flag_calls;effects.push_back("flags:"+std::to_string(ref.id));
            require(active_source!=nullptr,"flags setter retains actual source frame independently of target");
            return world.set_entity_flags(*active_source,ref,value);};
        services.create=[&](const EntityPosition& point,std::string& error){
            ++create_calls;created.push_back(point);effects.push_back("create");
            require(point.direction==0&&point.x%16==8&&point.y%16==8,"Fire constructor receives centered NORTH destination");
            if(refuse_constructor){error="fixture constructor failure";return false;}
            if(!normal_null_constructor)add(next_id++,EntityKind::object,point,0,ts1_fire_guid);
            return true;};
    }
    void add(std::int16_t id,EntityKind kind,EntityPosition pose,std::size_t person,std::uint32_t guid=42){
        require(world.create(id,kind,{0,person,kind==EntityKind::avatar?std::size_t{16}:std::size_t{0}})&&
            world.define_type(id,guid)&&world.set_position(id,pose)&&world.declare_object_word(id,8,0)&&
            world.declare_object_word(id,40,0),"construct independent fire fixture body");
        if(kind==EntityKind::avatar&&id!=1)other_avatar_threads.emplace(id,std::make_unique<Vm>(RoutineSet{},world.vm_memory()));
    }
    void burnable(std::int16_t id,std::int16_t flags=0){
        require(world.declare_object_word(id,40,32)&&world.declare_object_word(id,8,flags),"author burnable flags");
    }
    void threshold(std::int32_t value){auto state=clock.snapshot();state.fire_percent=value;require(clock.restore(state),"restore explicit fixture threshold");}
    PrimitiveOutcome run(std::uint8_t type=0,std::int16_t callee=1,std::uint8_t busy_byte=0){
        Instruction op{9,254,255,{type,busy_byte,0,0,0,0,0,0}};
        vm=std::make_unique<Vm>(RoutineSet{{routine_key(4096),routine({op})}},world.vm_memory(),VmLimits{},
            [&](const VmFrame& f,const Instruction& i,const Ts1Temps&,const VmDispatch& d){
                active_source=&f;services.instruction_lease=d.origin->instruction_lease(f);
                if(before_burn)before_burn(f);outcome=execute_ts1_burn(world,clock,f,i,d,services);active_source=nullptr;return outcome;});
        require(vm->start(routine_key(4096),{1,callee,2,0})==VmStatus::running,"start actual Burn source VM");
        vm->run(1);return outcome;
    }
};
void source_order(){
    {Fixture f;f.burnable(2);auto result=f.run();
     require(result.disposition==PrimitiveDisposition::goto_true&&f.random_calls==0&&f.create_calls==1&&
       f.clock.snapshot().fire_percent==20000&&(f.world.find(2)->object_words.at(8)&512),"Type0 burns without spread RNG");
     require(f.effects[0]=="pool"&&f.effects[1]=="create"&&f.effects[2]=="flags:2","initial Fire precedes object busy/burn processing");}
    {Fixture f;f.threshold(500);f.draw=500;auto result=f.run(2);
     require(result.disposition==PrimitiveDisposition::goto_false&&f.random_calls==1&&f.effects.size()==1&&
       f.clock.snapshot().fire_percent==500,"failed chance precedes pool and leaves threshold");}
    {Fixture f;f.threshold(900);f.pool=true;auto result=f.run(2);
     require(result.disposition==PrimitiveDisposition::goto_false&&f.random_calls==1&&f.create_calls==0&&
       f.clock.snapshot().fire_percent==900&&f.effects==std::vector<std::string>({"random","pool"}),"pool follows RNG and precedes decrement");}
    for(const auto type:{0,2,3,255}){Fixture f;f.threshold(200);f.services.position=[](WorldEntityReference){return std::optional<EntityPosition>{{-32768,-32768,1,0}};};
     auto result=f.run(static_cast<std::uint8_t>(type));
     require(result.disposition==PrimitiveDisposition::goto_false&&f.create_calls==0&&
       f.random_calls==static_cast<unsigned>(type!=0)&&f.clock.snapshot().fire_percent==(type?0:200),"OOW follows accepted-spread decrement with exact type default semantics");}
    for(unsigned direction=0;direction<8;++direction){Fixture f;
     require(f.world.set_position(2,{56,56,1,static_cast<std::uint8_t>(direction)}),"direction fixture");
     const auto result=f.run(1);auto expected=EntityPosition{56,56,1,0};
     if(direction==0)expected.y-=16;if(direction==2)expected.x+=16;if(direction==4)expected.y+=16;if(direction==6)expected.x-=16;
     require(result.disposition==PrimitiveDisposition::goto_true&&f.created.front()==expected,"Type1 offsets cardinal only; diagonal source leaves position unchanged");}
    {Fixture f;f.threshold(1000);f.refuse_constructor=true;auto result=f.run(2);
     require(result.disposition==PrimitiveDisposition::unsupported&&f.random_calls==1&&f.clock.snapshot().fire_percent==500&&f.create_calls==1,"constructor failure retains RNG/threshold ordering");}
    {Fixture f;f.normal_null_constructor=true;auto result=f.run();require(result.disposition==PrimitiveDisposition::goto_true&&f.create_calls==1,"source madeAFire records completed construction attempt even if constructor returns null");}
}
void physical_groups(){
    for(const std::int16_t flags:{std::int16_t{32},std::int16_t{2048}}){Fixture f;f.burnable(2,flags);const auto result=f.run(0,1,1);
     require(result.disposition==PrimitiveDisposition::goto_true&&f.create_calls==1&&f.flag_calls==0,"initial tile Fire precedes busy/fireproof exclusion; busy operand is ignored");}
    {Fixture f;f.burnable(2);const auto result=f.run(0,2);require(result.disposition==PrimitiveDisposition::goto_true&&f.flag_calls==0,
       "real avatar Thread Callee makes object in use even when occupied flag is clear");}
    {Fixture f;f.burnable(2);f.run(0,1);require(f.flag_calls==1,"StackObject-only reference does not imply source IsInUse");}
    {Fixture f;f.add(3,EntityKind::object,{40,24,1,0},0);f.add(4,EntityKind::object,{56,24,1,0},0);f.add(5,EntityKind::object,{72,24,2,0},0);
     const auto ref=[&](int id){const auto* e=f.world.find(static_cast<std::int16_t>(id));return WorldEntityReference{e->id,e->generation};};
     const std::vector<WorldEntityReference> members{ref(2),ref(4),ref(3),ref(5)};
     for(const auto member:members){f.groups[member.id]=members;f.burnable(member.id);}
     const auto result=f.run();
     require(result.disposition==PrimitiveDisposition::goto_true&&f.created.size()==3&&f.created[1].x==56&&f.created[2].x==40,
       "actual multitile order drives queue and constructors rather than random neighbor spread");
     require(!(f.world.find(5)->object_words.at(8)&512),"group elements on a different level do not burn");}
    {Fixture f;f.add(3,EntityKind::object,{24,24,1,0},0,ts1_fire_guid);f.burnable(2);const auto result=f.run();
     require(result.disposition==PrimitiveDisposition::goto_false&&f.create_calls==0&&(f.world.find(2)->object_words.at(8)&512),
       "existing Fire prevents duplicates but burnable flags still change");}
    {Fixture f;f.add(3,EntityKind::avatar,{24,24,1,0},101);f.burnable(3);
     require(f.world.set_person_data(3,68,1),"explicit ghost fixture");
     const auto result=f.run();require(result.disposition==PrimitiveDisposition::goto_true&&
       !(f.world.find(3)->object_words.at(8)&512),"ghost avatar skips independently after real IsInUse");}
    {Fixture f;f.burnable(2);auto stale=WorldEntityReference{2,f.world.find(2)->generation};
     f.services.group=[&](WorldEntityReference)->std::optional<std::vector<WorldEntityReference>>{return std::vector<WorldEntityReference>{{stale.id,stale.generation+1}};};
     const auto result=f.run();require(result.disposition==PrimitiveDisposition::unsupported&&f.flag_calls==0,"wrong-generation physical group cannot redirect mutation");}
}
void exact_anchor(){
    {Fixture f;f.before_burn=[&](const VmFrame& source){
        const auto original=vm_context_reference(source.context,VmEntityRole::stack_object);
        require(original.generation&&f.world.remove_retaining_body(original),"retain original exact source anchor");
        f.add(2,EntityKind::object,{88,88,1,0},0);f.burnable(2);
        require(f.world.find(2)->generation!=original.generation,"replacement numeric ID gets a fresh body");};
     const auto result=f.run();
     require(result.disposition==PrimitiveDisposition::goto_true&&f.created.size()==1&&f.created[0].x==24&&f.created[0].y==24&&
       !(f.world.find(2)->object_words.at(8)&512),"retained read-only anchor stays at original tile and cannot burn reused-ID replacement");}
    {Fixture f;f.before_burn=[&](const VmFrame& source){
        require(f.world.remove(source.context.stack_object),"remove unknown source anchor without retaining it");};
     f.threshold(1200);const auto result=f.run(2);
     require(result.disposition==PrimitiveDisposition::unsupported&&f.create_calls==0&&f.random_calls==1&&
       f.clock.snapshot().fire_percent==700,"truly missing target fails after earlier source RNG/pool/threshold effects");}
    {Fixture f;f.services.position=[](WorldEntityReference)->std::optional<EntityPosition>{return EntityPosition{2147483647,24,1,0};};
     const auto result=f.run(1);require(result.disposition==PrimitiveDisposition::unsupported&&f.create_calls==0,
       "invalid host scalar cannot overflow source offset arithmetic or teleport into lot");}
}
void flags_lifecycle(){
    World world;require(world.create(1,EntityKind::avatar,{0,101,16})&&world.define_type(1,42)&&
        world.declare_object_word(1,8,0),"flags lifecycle fixture");
    const WorldEntityReference victim{1,world.find(1)->generation};
    VmFrame original{};original.context={2,2,2,0};
    require(!world.set_entity_flags(original,victim,512)&&world.find(1)->object_words.at(8)==512,
        "missing lifecycle service reports failure after preserving source raw Burning transition");
    require(world.set_entity_flags(original,victim,512),"same-bit assignment requires no Reset service");
    require(!world.set_entity_flags(original,victim,0)&&world.find(1)->object_words.at(8)==0,
        "missing lifecycle service also rejects Burning fall after source raw publication");
    unsigned resets=0;bool accept=true;
    require(world.set_avatar_burning_observer([&](World& source,const VmFrame& frame,WorldEntityReference target,
            std::int16_t before,std::int16_t after){
        ++resets;require(&source==&world&& &frame==&original&&target==victim,
            "flags callback carries actual source frame and distinct exact victim");
        require(((std::uint16_t(before)^std::uint16_t(after))&512U)!=0&&
                source.find(target.id)->object_words.at(8)==after,"Reset observes already published transition");
        return accept;}),"bind explicit host lifecycle");
    require(world.set_entity_flags(original,victim,512)&&resets==1,"Burning rise dispatches Reset");
    require(world.set_entity_flags(original,victim,512)&&resets==1,"same Burning bit does not Reset");
    require(world.set_entity_flags(original,victim,0)&&resets==2,"Burning fall dispatches Reset too");
    accept=false;
    require(!world.set_entity_flags(original,victim,512)&&resets==3&&world.find(1)->object_words.at(8)==512,
        "Reset failure retains source raw flag publication and reports failure");
    require(!world.set_entity_flags(original,{victim.id,victim.generation+1},0)&&resets==3,
        "wrong-generation victim cannot redirect flags mutation");
}
}
int main(){try{
 source_order();physical_groups();exact_anchor();flags_lifecycle();
 Fixture f;VmFrame frame{};frame.context={1,1,2,0};Instruction op{9,254,255,{}};
 const auto denied=execute_ts1_burn(f.world,f.clock,frame,op,{nullptr,1,false},f.services);
 require(denied.disposition==PrimitiveDisposition::unsupported&&f.random_calls==0&&f.create_calls==0,"fabricated dispatch grants no Burn authority");
 std::cout<<checks<<" TS1 fire checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
