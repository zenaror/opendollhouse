#include "odh/interaction.hpp"
#include "odh/sleep.hpp"
#include "odh/tuning.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <iostream>
#include <memory>

namespace {
struct Resource {std::string archive,entry;};
struct Loaded {
    odh::RoutineSet routines;
    odh::TuningTables tuning;
    std::optional<odh::ObjdBinding> binding;
    std::optional<odh::ObjdFunctions> functions;
    std::optional<odh::Glob> glob;
    std::optional<std::uint16_t> object_type;
};
std::string fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return s;}
Loaded load(const Resource& resource,odh::RoutineScope role,std::optional<std::uint16_t> definition={}) {
    Loaded result;
    with_far_entry(resource.archive,resource.entry,[&](const auto& view){
        odh::iff_chunks(view,[&](const auto& chunk){
            if(chunk.type=="BCON") {
                if(!result.tuning.emplace(chunk.id,odh::read_bcon(chunk.payload)).second)throw odh::FormatError("ambiguous BCON ID in explicit resource");
            }
            if(chunk.type=="BHAV") {
                const auto key=odh::routine_key(chunk.id);
                if(key.scope!=role)throw odh::FormatError("BHAV outside explicit resource role");
                if(!result.routines.emplace(key,odh::read_bhav(chunk.payload)).second)throw odh::FormatError("ambiguous BHAV");
            }
            if(definition&&chunk.type=="OBJD"&&chunk.id==*definition){
                if(result.binding)throw odh::FormatError("ambiguous OBJD");
                result.binding=odh::read_objd_binding(chunk.payload);result.functions=odh::read_objd_functions(chunk.payload);
                if(result.binding->version==138)result.object_type=chunk.payload.le16(18);
            }
            if(role==odh::RoutineScope::private_object&&chunk.type=="GLOB"&&chunk.id==128){
                if(result.glob)throw odh::FormatError("ambiguous GLOB128");result.glob=odh::read_glob(chunk.payload);
            }
        });
    });
    if(definition&&(!result.binding||!result.functions))throw odh::FormatError("selected OBJD absent");
    if(result.binding&&result.binding->master_id!=0)throw odh::FormatError("multitile identity unsupported");
    return result;
}
std::int16_t signed_word(std::uint16_t v){return static_cast<std::int16_t>(v<32768?static_cast<int>(v):static_cast<int>(v)-65536);}
}
int main(int argc,char** argv) {
 try {
    std::optional<Resource> avatar,semi,globals,action;
    std::optional<std::uint16_t> avatar_def,action_def,action_routine,flags,age,routine;
    std::uint16_t ticks_limit=256;bool ticks_set=false;
    for(int n=1;n<argc;++n){const std::string option=argv[n];
        if(option=="--avatar"&&!avatar&&n+3<argc){avatar=Resource{argv[n+1],argv[n+2]};avatar_def=selector(argv[n+3]);n+=3;}
        else if(option=="--semiglobal"&&!semi&&n+2<argc){semi=Resource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--global"&&!globals&&n+2<argc){globals=Resource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--action"&&!action&&n+4<argc){action=Resource{argv[n+1],argv[n+2]};action_def=selector(argv[n+3]);action_routine=selector(argv[n+4]);n+=4;}
        else if(option=="--ticks"&&!ticks_set&&n+1<argc){ticks_limit=selector(argv[++n]);ticks_set=true;}
        else if(option=="--flags"&&!flags&&n+1<argc)flags=selector(argv[++n]);
        else if(option=="--age"&&!age&&n+1<argc)age=selector(argv[++n]);
        else if(option=="--routine"&&!routine&&n+1<argc)routine=selector(argv[++n]);
        else throw odh::FormatError("invalid or repeated person host option");
    }
    if(!avatar||!semi||!globals||ticks_limit>4096||(age&&*age>32767))
        throw odh::FormatError("usage: --avatar FAR ENTRY OBJD --semiglobal FAR ENTRY --global FAR ENTRY [--routine ID] [--action FAR ENTRY OBJD ROUTINE] [--flags WORD] [--age YEARS] [--ticks 0..4096]");
    auto person=load(*avatar,odh::RoutineScope::private_object,*avatar_def);
    if(person.binding->version!=138||person.object_type!=2)throw odh::FormatError("person constructor profile requires OBJD138 ObjectType2");
    if(!person.glob||person.glob->name.empty()||person.glob->name.find_first_of("/\\:")!=std::string::npos||
       person.glob->name.find('\0')!=std::string::npos||fold(person.glob->name+".iff")!=fold(semi->entry))
        throw odh::FormatError("semiglobal does not match flat Pascal GLOB128 binding");
    if(person.functions->uses_function_table!=0)throw odh::FormatError("avatar OBJf dispatch unsupported");
    const auto root=routine.value_or(person.functions->main);
    if(root==0||odh::routine_key(root).scope!=odh::RoutineScope::private_object||person.routines.find(odh::routine_key(root))==person.routines.end())
        throw odh::FormatError("avatar root routine absent from explicit private owner");
    auto semi_data=load(*semi,odh::RoutineScope::semiglobal);
    person.routines.insert(semi_data.routines.begin(),semi_data.routines.end());
    auto shared=load(*globals,odh::RoutineScope::global);
    std::optional<Loaded> action_data;
    if(action){action_data=load(*action,odh::RoutineScope::private_object,*action_def);
        if(odh::routine_key(*action_routine).scope!=odh::RoutineScope::private_object||action_data->routines.find(odh::routine_key(*action_routine))==action_data->routines.end())
            throw odh::FormatError("action routine absent from explicit private owner");
        if(action_data->glob&&!action_data->glob->name.empty())throw odh::FormatError("action semiglobal not supplied by this host profile");
    }
    odh::World world;
    if(!world.create(1,odh::EntityKind::avatar,{0,101,3,16})||!world.define_type(1,person.binding->guid))throw odh::FormatError("avatar fixture construction failed");
    // Explicit reference constructor subset, not completed native Init.
    for(std::int16_t index=0;index<79;++index) {
        if(index==1||index==2||index==3||index==11||index==25||index==26||index==29||index==41||index==62||index==67)continue;
        if(!world.declare_object_word(1,index,0))throw odh::FormatError("reference constructor object word failed");
    }
    if(!world.declare_object_word(1,8,signed_word(flags.value_or(256))))throw odh::FormatError("flags fixture construction failed");
    if(age&&!world.set_person_data(1,58,static_cast<std::int16_t>(*age)))throw odh::FormatError("age fixture construction failed");
    odh::TuningResources tuning_resources;
    tuning_resources.globals=std::move(shared.tuning);
    tuning_resources.owners.emplace(1,odh::TuningOwner{std::move(person.tuning),std::move(semi_data.tuning),odh::TuningSemiState::loaded});
    if(action)tuning_resources.owners.emplace(2,odh::TuningOwner{std::move(action_data->tuning),{},odh::TuningSemiState::absent});
    const odh::BconTuning tuning(std::move(tuning_resources));
    std::vector<odh::TuningValue> tuning_reads;
    const auto world_memory=world.vm_memory();auto memory=world_memory;
    memory.read=[&](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index)->std::optional<std::int16_t> {
        if(scope!=26)return world_memory.read(frame,scope,index);
        const auto value=tuning.read(frame.context.code_owner,index);
        if(!value)return {};
        if(tuning_reads.size()>=20000)throw odh::FormatError("tuning read log exceeds bounded host profile");
        tuning_reads.push_back(*value);return value->value;
    };
    odh::RoutineOwners owners{{1,std::move(person.routines)}};
    if(action){owners.emplace(2,std::move(action_data->routines));
        if(!world.create(2,odh::EntityKind::object)||!world.define_type(2,action_data->binding->guid))throw odh::FormatError("action target construction failed");}
    std::uint64_t rng_requests=0;
    odh::Sleep sleep(world,[&]{++rng_requests;});odh::InteractionQueue queue(world,1);
    const auto sleep_handler=sleep.primitive(),queue_handler=queue.primitive();
    odh::Vm vm(std::move(shared.routines),memory,{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==0)return sleep_handler(f,i,t,d);
        if(i.opcode==17)return queue_handler(f,i,t,d);
        return odh::PrimitiveOutcome{};
    },std::move(owners));
    if(!sleep.bind(vm)||!queue.bind(vm))throw odh::FormatError("person controllers could not bind");
    vm.start(odh::routine_key(root),{1,1,1,1});
    if(action&&vm.status()!=odh::VmStatus::fault&&!queue.enqueue(odh::routine_key(*action_routine),{1,2,2,2}))throw odh::FormatError(queue.diagnostic());
    std::vector<odh::VmTrace> trace;bool host_failed=false;std::string host_diagnostic;
    std::uint64_t ticks=0;const auto execute=[&] {
        if(!queue.observe()){host_failed=true;host_diagnostic=queue.diagnostic();return;}
        vm.run(vm.steps()<10000?10000-vm.steps():0,[&](const auto& record){trace.push_back(record);});
        if(!queue.observe()){host_failed=true;host_diagnostic=queue.diagnostic();}
    };
    execute();
    while(!host_failed&&ticks<ticks_limit&&vm.steps()<10000&&
          (vm.status()==odh::VmStatus::waiting||vm.status()==odh::VmStatus::yielded)) {
        ++ticks;
        if(!queue.tick(ticks)||!sleep.tick(ticks)){host_failed=true;host_diagnostic=queue.diagnostic().empty()?sleep.diagnostic():queue.diagnostic();break;}
        execute();
    }
    const auto status=vm.status();const bool tick_exhausted=!host_failed&&ticks==ticks_limit&&
        (status==odh::VmStatus::waiting||status==odh::VmStatus::yielded);
    const bool action_incomplete=action.has_value() && (!queue.completion() || queue.size()!=0);
    const bool instruction_exhausted=vm.steps()>=10000 && (status==odh::VmStatus::waiting||status==odh::VmStatus::yielded||status==odh::VmStatus::budget_exhausted);
    std::cout<<"{\"evidence\":\"ODH_native_BHAV_constructed_person_host\",\"initialization\":\"full Init/entry8/body-string pipeline not executed; explicit root may be Init\",\"avatar\":{\"archive\":"<<json_bytes(avatar->archive)<<",\"entry\":"<<json_bytes(avatar->entry)
        <<",\"objd_id\":"<<*avatar_def<<",\"guid\":"<<person.binding->guid<<",\"owner\":1,\"main\":"<<person.functions->main<<",\"root\":"<<root
        <<",\"semiglobal_archive\":"<<json_bytes(semi->archive)<<",\"semiglobal_entry\":"<<json_bytes(semi->entry)<<",\"glob_name\":"<<json_bytes(person.glob->name)
        <<"},\"global\":{\"archive\":"<<json_bytes(globals->archive)<<",\"entry\":"<<json_bytes(globals->entry)<<"},\"trace\":[";
    for(std::size_t n=0;n<trace.size();++n){if(n)std::cout<<',';const auto& t=trace[n];
        std::cout<<"{\"step\":"<<t.step<<",\"depth\":"<<t.depth<<",\"owner\":"<<t.code_owner<<",\"routine\":"<<t.key.id<<",\"pc\":"<<t.pc
            <<",\"opcode\":"<<t.instruction.opcode<<",\"true\":"<<static_cast<unsigned>(t.instruction.on_true)<<",\"false\":"<<static_cast<unsigned>(t.instruction.on_false)<<",\"operands\":[";
        for(std::size_t j=0;j<8;++j){if(j)std::cout<<',';std::cout<<static_cast<unsigned>(t.instruction.operands[j]);}std::cout<<"]}";
    }
    std::cout<<"],\"status\":"<<json_bytes(odh::vm_status_name(status))<<",\"diagnostic\":"<<json_bytes(vm.diagnostic())
        <<",\"host_failed\":"<<(host_failed?"true":"false")<<",\"host_diagnostic\":"<<json_bytes(host_diagnostic)<<",\"steps\":"<<vm.steps()<<",\"ticks\":"<<ticks
        <<",\"tick_budget_exhausted\":"<<(tick_exhausted?"true":"false")<<",\"instruction_budget_exhausted\":"<<(instruction_exhausted?"true":"false")<<",\"stack_depth\":"<<vm.stack().size()<<",\"rng_cycle_requests\":"<<rng_requests
        <<",\"rng_backend\":\"not_implemented\",\"queue_size\":"<<queue.size()<<",\"queue_active\":"<<(queue.active()?"true":"false")<<",\"action_incomplete\":"<<(action_incomplete?"true":"false")<<",\"action\":";
    if(action)std::cout<<"{\"archive\":"<<json_bytes(action->archive)<<",\"entry\":"<<json_bytes(action->entry)<<",\"objd_id\":"<<*action_def<<",\"guid\":"<<action_data->binding->guid<<",\"owner\":2,\"routine\":"<<*action_routine<<'}';else std::cout<<"null";
    std::cout<<",\"completion\":";
    if(queue.completion()){const auto& c=*queue.completion();std::cout<<"{\"uid\":"<<c.uid<<",\"ticket\":"<<c.ticket<<",\"tick\":"<<c.tick<<",\"result\":"<<(c.result?"true":"false")<<'}';}else std::cout<<"null";
    std::cout<<",\"tuning_profile\":\"explicit BCON only; no OTF or entity replacement backend; missing remains unsupported\",\"tuning_reads\":[";
    for(std::size_t n=0;n<tuning_reads.size();++n){if(n)std::cout<<',';const auto& v=tuning_reads[n];
        const auto scope=v.address.scope==odh::RoutineScope::global?"global":v.address.scope==odh::RoutineScope::semiglobal?"semiglobal":"private";
        std::cout<<"{\"owner\":"<<v.owner<<",\"scope\":"<<json_bytes(scope)<<",\"table\":"<<v.address.table<<",\"index\":"<<static_cast<unsigned>(v.address.index)
            <<",\"value\":"<<v.value<<",\"semiglobal_fallback\":"<<(v.semiglobal_fallback?"true":"false")<<'}';
    }
    std::cout<<']';
    const auto* actor=world.find(1);
    std::cout<<",\"constructor_profile\":\"FreeSO 4c6b3e8 subset; PersonData101/MotiveData16/Contained3; raw ObjectData0..78 except derived getters; World identity/container/slot only\",\"flags_initial\":"<<flags.value_or(256)
        <<",\"age_initial\":"<<age.value_or(0)<<",\"actor_state\":{\"flags\":"<<actor->object_words.at(8)<<",\"age\":"<<actor->person_data[58]
        <<",\"autonomy\":"<<actor->person_data[36]<<",\"weight\":"<<actor->object_words.at(27)<<",\"support_strength\":"<<actor->object_words.at(28)
        <<",\"rotation_notches\":"<<actor->object_words.at(23)<<",\"flag_field2\":"<<actor->object_words.at(40)<<",\"room_placement\":"<<actor->object_words.at(30)<<",\"hunger\":"<<actor->motives[7]<<"}";
    std::cout<<",\"stack\":[";for(std::size_t n=0;n<vm.stack().size();++n){if(n)std::cout<<',';const auto& f=vm.stack()[n];
        std::cout<<"{\"owner\":"<<f.context.code_owner<<",\"routine\":"<<f.key.id<<",\"pc\":"<<f.pc<<",\"caller\":"<<f.context.caller<<",\"callee\":"<<f.context.callee<<",\"stack_object\":"<<f.context.stack_object<<'}';}
    std::cout<<"]}\n";
    return host_failed||status==odh::VmStatus::fault||status==odh::VmStatus::budget_exhausted||tick_exhausted||instruction_exhausted||action_incomplete?3:0;
 }catch(const std::exception& e){std::cerr<<"odh-person: "<<e.what()<<'\n';return 2;}
}
