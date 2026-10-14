#include "odh/interaction.hpp"
#include "odh/sleep.hpp"
#include "odh/tuning.hpp"
#include "odh/lot.hpp"
#include "odh/lot_context.hpp"
#include "odh/random.hpp"
#include "odh/avatar_body.hpp"
#include "odh/neighbor_context.hpp"
#include "resource.hpp"
#include "json.hpp"
#include <iostream>
#include <memory>
#include <charconv>
#include <algorithm>

namespace {
struct Resource {std::string archive,entry;};
struct Loaded {
    odh::RoutineSet routines;
    odh::TuningTables tuning;
    std::optional<odh::ObjdBinding> binding;
    std::optional<odh::ObjdFunctions> functions;
    std::optional<odh::ObjdStartup> startup;
    std::optional<odh::Glob> glob;
    std::optional<std::uint16_t> object_type;
};
std::string fold(std::string s){for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return s;}
Loaded load(const Resource& resource,odh::RoutineScope role,std::optional<std::uint16_t> definition={},bool startup=false) {
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
                if(startup)result.startup=odh::read_objd_startup(chunk.payload);
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
std::map<std::uint16_t,odh::StringTable> load_strings(const Resource& resource,const std::vector<std::uint16_t>& ids){
    std::map<std::uint16_t,odh::StringTable> result;
    with_far_entry(resource.archive,resource.entry,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="STR#"||std::find(ids.begin(),ids.end(),chunk.id)==ids.end())return;
        if(!result.emplace(chunk.id,odh::read_strings(chunk.payload)).second)throw odh::FormatError("ambiguous startup STR# ID");
    });});return result;
}
std::int16_t signed_word(std::uint16_t v){return static_cast<std::int16_t>(v<32768?static_cast<int>(v):static_cast<int>(v)-65536);}
std::uint64_t seed_selector(const std::string& text){
    if(text.empty())throw odh::FormatError("empty RNG seed");
    const bool hex=text.size()>2&&text[0]=='0'&&(text[1]=='x'||text[1]=='X');
    std::uint64_t value=0;const auto result=std::from_chars(text.data()+(hex?2:0),text.data()+text.size(),value,hex?16:10);
    if(result.ec!=std::errc{}||result.ptr!=text.data()+text.size())throw odh::FormatError("RNG seed outside uint64 profile");
    return value;
}
}
int main(int argc,char** argv) {
 try {
    std::optional<Resource> avatar,semi,globals,action;
    std::optional<std::string> lot_path,zoning_path,neighborhood_path;
    std::optional<std::uint16_t> lot_id;
    std::optional<std::uint64_t> seed;
    std::optional<std::uint16_t> avatar_def,action_def,action_routine,flags,age,routine;
    std::uint16_t ticks_limit=256;bool ticks_set=false,initialize=false;
    for(int n=1;n<argc;++n){const std::string option=argv[n];
        if(option=="--avatar"&&!avatar&&n+3<argc){avatar=Resource{argv[n+1],argv[n+2]};avatar_def=selector(argv[n+3]);n+=3;}
        else if(option=="--semiglobal"&&!semi&&n+2<argc){semi=Resource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--global"&&!globals&&n+2<argc){globals=Resource{argv[n+1],argv[n+2]};n+=2;}
        else if(option=="--action"&&!action&&n+4<argc){action=Resource{argv[n+1],argv[n+2]};action_def=selector(argv[n+3]);action_routine=selector(argv[n+4]);n+=4;}
        else if(option=="--ticks"&&!ticks_set&&n+1<argc){ticks_limit=selector(argv[++n]);ticks_set=true;}
        else if(option=="--flags"&&!flags&&n+1<argc)flags=selector(argv[++n]);
        else if(option=="--age"&&!age&&n+1<argc)age=selector(argv[++n]);
        else if(option=="--routine"&&!routine&&n+1<argc)routine=selector(argv[++n]);
        else if(option=="--lot"&&!lot_path&&n+2<argc){lot_path=argv[n+1];lot_id=selector(argv[n+2]);n+=2;}
        else if(option=="--zoning"&&!zoning_path&&n+1<argc)zoning_path=argv[++n];
        else if(option=="--seed"&&!seed&&n+1<argc)seed=seed_selector(argv[++n]);
        else if(option=="--initialize"&&!initialize)initialize=true;
        else if(option=="--neighborhood"&&!neighborhood_path&&n+1<argc)neighborhood_path=argv[++n];
        else throw odh::FormatError("invalid or repeated person host option");
    }
    if(!avatar||!semi||!globals||ticks_limit>4096||(age&&*age>32767)||(lot_id&&(*lot_id==0||*lot_id>32767))||(zoning_path&&!lot_path)||(initialize&&(routine||age||flags)))
        throw odh::FormatError("usage: --avatar FAR ENTRY OBJD --semiglobal FAR ENTRY --global FAR ENTRY [--initialize | --routine ID] [--action FAR ENTRY OBJD ROUTINE] [--lot HOUSE_IFF LOT_ID] [--zoning ZONING_IFF] [--neighborhood NBRS_IFF] [--seed UINT64] [--flags WORD] [--age YEARS] [--ticks 0..4096]");
    std::optional<odh::Simi> simi;std::optional<odh::LotZoning> zoning;
    if(lot_path)with_iff_file(*lot_path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="SIMI"||chunk.id!=1)return;
        if(simi)throw odh::FormatError("ambiguous SIMI1");simi=odh::read_simi(chunk.payload);
    });});
    if(lot_path&&!simi)throw odh::FormatError("SIMI1 absent in selected house");
    if(zoning_path)with_iff_file(*zoning_path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="STR#"||chunk.id!=1)return;
        if(zoning)throw odh::FormatError("ambiguous zoning STR#1");zoning=odh::read_lot_zoning(odh::read_strings(chunk.payload));
    });});
    if(zoning_path&&!zoning)throw odh::FormatError("zoning STR#1 absent");
    std::optional<odh::LotContext> lot;
    if(simi)lot.emplace(static_cast<std::int16_t>(*lot_id),simi->global_data,std::move(zoning));
    std::vector<std::pair<std::int16_t,std::int16_t>> global_reads;
    std::vector<odh::LotZoneResult> zoning_reads;
    std::optional<odh::Nbrs> nbrs;
    if(neighborhood_path)with_iff_file(*neighborhood_path,[&](const auto& view){odh::iff_chunks(view,[&](const auto& chunk){
        if(chunk.type!="NBRS"||chunk.id!=1)return;
        if(nbrs)throw odh::FormatError("ambiguous NBRS1");nbrs=odh::read_nbrs(chunk.payload);
    });});
    if(neighborhood_path&&!nbrs)throw odh::FormatError("NBRS1 absent");
    std::optional<odh::NeighborContext> neighbors;
    if(nbrs)neighbors.emplace(std::move(*nbrs));
    std::vector<odh::NeighborNext> neighbor_next;
    std::vector<odh::NeighborDataValue> neighbor_reads;
    auto person=load(*avatar,odh::RoutineScope::private_object,*avatar_def,initialize);
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
    std::optional<odh::AvatarBodyStrings> body;
    std::optional<odh::StringTable> body_walk,body_swim,private_walk;
    if(initialize){
        auto strings=load_strings(*avatar,{person.startup->body_strings,150});
        const auto table=strings.find(person.startup->body_strings);
        if(table==strings.end())throw odh::FormatError("startup BodySTR absent");
        body=odh::parse_avatar_body_strings(table->second);
        if(body->avatar_type!="adult")throw odh::FormatError("startup constructor profile requires adult BodySTR type");
        const auto special=strings.find(150);if(special!=strings.end())private_walk=std::move(special->second);
        const bool child=*body->age<18;
        const std::uint16_t walk_id=child?151:150,swim_id=child?160:158;
        auto animation_strings=load_strings(*globals,{walk_id,swim_id});
        if(!animation_strings.count(walk_id)||!animation_strings.count(swim_id))throw odh::FormatError("startup global walk/swim STR# absent");
        body_walk=std::move(animation_strings.at(walk_id));body_swim=std::move(animation_strings.at(swim_id));
        for(const auto id:{person.startup->init,person.startup->dynamic_multitile})if(id>255){
            const auto key=odh::routine_key(id);const auto& bank=key.scope==odh::RoutineScope::global?shared.routines:person.routines;
            if(!bank.count(key))throw odh::FormatError("startup routine absent from explicit owner");
        }
    }
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
        if(scope==32){const auto value=neighbors?neighbors->person_data(frame.context.stack_object,index):std::nullopt;
            if(!value)return {};
            if(neighbor_reads.size()>=20000)throw odh::FormatError("neighbor read log exceeds bounded host profile");
            neighbor_reads.push_back(*value);return value->value;}
        if(scope==6){const auto value=lot?lot->read(index):std::nullopt;
            if(value){if(global_reads.size()>=20000)throw odh::FormatError("global read log exceeds bounded host profile");global_reads.emplace_back(index,*value);}return value;}
        if(scope!=26)return world_memory.read(frame,scope,index);
        const auto value=tuning.read(frame.context.code_owner,index);
        if(!value)return {};
        if(tuning_reads.size()>=20000)throw odh::FormatError("tuning read log exceeds bounded host profile");
        tuning_reads.push_back(*value);return value->value;
    };
    memory.write=[&](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value){
        if(scope==6)return lot&&lot->write(index,value);
        return world_memory.write(frame,scope,index,value);
    };
    odh::RoutineOwners owners{{1,std::move(person.routines)}};
    if(action){owners.emplace(2,std::move(action_data->routines));
        if(!world.create(2,odh::EntityKind::object)||!world.define_type(2,action_data->binding->guid))throw odh::FormatError("action target construction failed");}
    std::uint64_t rng_requests=0;
    odh::RandomState random(seed.value_or(1));std::vector<odh::RandomRecord> random_reads;
    odh::Sleep sleep(world,[&]{++rng_requests;random.next(1);});odh::InteractionQueue queue(world,1);
    const auto random_handler=odh::random_primitive(random,memory,[&](const odh::RandomRecord& value){
        if(random_reads.size()>=10000)throw odh::FormatError("random log exceeds bounded host profile");random_reads.push_back(value);
    });
    const auto sleep_handler=sleep.primitive(),queue_handler=queue.primitive();
    const auto lot_handler=lot?lot->primitive([&](const auto& value){
        if(zoning_reads.size()>=10000)throw odh::FormatError("zoning read log exceeds bounded host profile");zoning_reads.push_back(value);
    }):odh::VmPrimitive{};
    const auto neighbor_handler=neighbors?neighbors->primitive(memory,[&](const auto& value){
        if(neighbor_next.size()>=10000)throw odh::FormatError("neighbor enumeration log exceeds bounded host profile");neighbor_next.push_back(value);
    }):odh::VmPrimitive{};
    odh::Vm vm(std::move(shared.routines),memory,{},[&](const auto& f,const auto& i,const auto& t,const auto& d){
        if(i.opcode==0)return sleep_handler(f,i,t,d);
        if(i.opcode==17)return queue_handler(f,i,t,d);
        if(i.opcode==8)return random_handler(f,i,t,d);
        if(i.opcode==1&&lot_handler)return lot_handler(f,i,t,d);
        if(i.opcode==31&&neighbor_handler)return neighbor_handler(f,i,t,d);
        return odh::PrimitiveOutcome{};
    },std::move(owners));
    if(!sleep.bind(vm)||!queue.bind(vm))throw odh::FormatError("person controllers could not bind");
    std::vector<odh::VmTrace> trace;bool host_failed=false;std::string host_diagnostic;
    std::vector<std::string> trace_phases;
    struct StartupStage {std::string phase;std::uint16_t routine;odh::VmStatus status;std::uint64_t steps;std::string diagnostic;};
    std::vector<StartupStage> stages;
    std::uint64_t startup_steps=0;
    std::optional<odh::AvatarBodyDescriptor> body_state;
    std::optional<std::uint64_t> body_seed_before,body_seed_after;
    const auto trace_in=[&](const std::string& phase){return [&,phase](const odh::VmTrace& record){trace.push_back(record);trace_phases.push_back(phase);};};
    if(initialize){
        for(const auto& entry:std::vector<std::pair<std::string,std::uint16_t>>{{"init",person.startup->init},{"dynamic_multitile",person.startup->dynamic_multitile}}){
            if(entry.second<=255){stages.push_back({entry.first,entry.second,odh::VmStatus::ready,0,"entry ID<=255 skipped by reference"});continue;}
            vm.start(odh::routine_key(entry.second),{1,1,0,1},{0,0,0,0});
            vm.run(10000-startup_steps,trace_in(entry.first));
            stages.push_back({entry.first,entry.second,vm.status(),vm.steps(),vm.diagnostic()});
            if(vm.status()!=odh::VmStatus::returned_true&&vm.status()!=odh::VmStatus::returned_false){
                host_failed=true;host_diagnostic="startup entry did not return synchronously";break;
            }
            startup_steps+=vm.steps();
        }
    }
    if(!host_failed){
        // Stack Main before post-body initialization, then dispatch its first
        // instruction only after the body data stage has completed.
        vm.start(odh::routine_key(root),{1,1,static_cast<std::int16_t>(initialize?0:1),1});
        if(initialize&&vm.status()!=odh::VmStatus::fault){
            try{body_seed_before=random.seed();body_state=odh::apply_avatar_body(world,1,random,body,*body_walk,*body_swim,private_walk);body_seed_after=random.seed();}
            catch(const std::exception& e){host_failed=true;host_diagnostic=e.what();}
        }
        if(action&&!host_failed&&vm.status()!=odh::VmStatus::fault&&!queue.enqueue(odh::routine_key(*action_routine),{1,2,2,2}))throw odh::FormatError(queue.diagnostic());
    }
    const auto total_steps=[&]{return startup_steps+vm.steps();};
    std::uint64_t ticks=0;const auto execute=[&] {
        if(host_failed)return;
        if(!queue.observe()){host_failed=true;host_diagnostic=queue.diagnostic();return;}
        vm.run(total_steps()<10000?10000-total_steps():0,trace_in(initialize?"main":"root_probe"));
        if(!queue.observe()){host_failed=true;host_diagnostic=queue.diagnostic();}
    };
    execute();
    while(!host_failed&&ticks<ticks_limit&&total_steps()<10000&&
          (vm.status()==odh::VmStatus::waiting||vm.status()==odh::VmStatus::yielded)) {
        ++ticks;
        if(!queue.tick(ticks)||!sleep.tick(ticks)){host_failed=true;host_diagnostic=queue.diagnostic().empty()?sleep.diagnostic():queue.diagnostic();break;}
        execute();
    }
    const auto status=vm.status();const bool tick_exhausted=!host_failed&&ticks==ticks_limit&&
        (status==odh::VmStatus::waiting||status==odh::VmStatus::yielded);
    const bool action_incomplete=action.has_value() && (!queue.completion() || queue.size()!=0);
    const bool instruction_exhausted=total_steps()>=10000 && (status==odh::VmStatus::waiting||status==odh::VmStatus::yielded||status==odh::VmStatus::budget_exhausted);
    std::cout<<"{\"evidence\":\"ODH_native_BHAV_constructed_person_host\",\"initialization\":"<<json_bytes(initialize?
        "ordered Init0/entry8/Main stack/postBodySTR reference subset; appearance resources, special tuning and native save restoration not initialized":
        "full Init/entry8/body-string pipeline not executed; explicit root may be Init")<<",\"startup_requested\":"<<(initialize?"true":"false")
        <<",\"startup_steps\":"<<startup_steps<<",\"startup_stages\":[";
    for(std::size_t n=0;n<stages.size();++n){if(n)std::cout<<',';const auto& stage=stages[n];
        std::cout<<"{\"phase\":"<<json_bytes(stage.phase)<<",\"routine\":"<<stage.routine<<",\"status\":"<<json_bytes(odh::vm_status_name(stage.status))<<",\"steps\":"<<stage.steps<<",\"diagnostic\":"<<json_bytes(stage.diagnostic)<<'}';}
    std::cout<<"],\"body_initialized\":"<<(body_state?"true":"false")<<",\"body_state\":";
    if(body_state){const auto& state=*body_state;
        std::cout<<"{\"age\":"<<*state.body->age<<",\"gender\":";
        if(state.body->gender)std::cout<<*state.body->gender;else std::cout<<"null";
        std::cout<<",\"avatar_type\":"<<json_bytes(state.body->avatar_type)<<",\"body\":"<<json_bytes(state.body->body)<<",\"head\":"<<json_bytes(state.body->head)
            <<",\"phenotype\":\"TS1 light reference policy\",\"appearance_resources_resolved\":false,\"selected_name\":"<<(state.selected_name?json_bytes(*state.selected_name):"null")
            <<",\"seed_before\":"<<*body_seed_before<<",\"seed_after\":"<<*body_seed_after<<",\"walk_table\":"<<state.global_walk_id<<",\"swim_table\":"<<state.global_swim_id<<",\"hands\":[";
        for(std::size_t n=0;n<6;++n){if(n)std::cout<<',';std::cout<<json_bytes(state.body->hands[n]);}
        std::cout<<"],\"walk\":[";for(std::size_t n=0;n<50;++n){if(n)std::cout<<',';std::cout<<json_bytes(state.walk[n]);}
        std::cout<<"],\"swim\":[";for(std::size_t n=0;n<50;++n){if(n)std::cout<<',';std::cout<<json_bytes(state.swim[n]);}std::cout<<"]}";
    }else std::cout<<"null";
    std::cout<<",\"avatar\":{\"archive\":"<<json_bytes(avatar->archive)<<",\"entry\":"<<json_bytes(avatar->entry)
        <<",\"objd_id\":"<<*avatar_def<<",\"guid\":"<<person.binding->guid<<",\"owner\":1,\"main\":"<<person.functions->main<<",\"root\":"<<root
        <<",\"semiglobal_archive\":"<<json_bytes(semi->archive)<<",\"semiglobal_entry\":"<<json_bytes(semi->entry)<<",\"glob_name\":"<<json_bytes(person.glob->name)
        <<"},\"global\":{\"archive\":"<<json_bytes(globals->archive)<<",\"entry\":"<<json_bytes(globals->entry)<<"},\"trace\":[";
    for(std::size_t n=0;n<trace.size();++n){if(n)std::cout<<',';const auto& t=trace[n];
        std::cout<<"{\"phase\":"<<json_bytes(trace_phases[n])<<",\"step\":"<<t.step<<",\"depth\":"<<t.depth<<",\"owner\":"<<t.code_owner<<",\"routine\":"<<t.key.id<<",\"pc\":"<<t.pc
            <<",\"opcode\":"<<t.instruction.opcode<<",\"true\":"<<static_cast<unsigned>(t.instruction.on_true)<<",\"false\":"<<static_cast<unsigned>(t.instruction.on_false)<<",\"operands\":[";
        for(std::size_t j=0;j<8;++j){if(j)std::cout<<',';std::cout<<static_cast<unsigned>(t.instruction.operands[j]);}std::cout<<"]}";
    }
    std::cout<<"],\"status\":"<<json_bytes(odh::vm_status_name(status))<<",\"diagnostic\":"<<json_bytes(vm.diagnostic())
        <<",\"host_failed\":"<<(host_failed?"true":"false")<<",\"host_diagnostic\":"<<json_bytes(host_diagnostic)<<",\"steps\":"<<total_steps()<<",\"vm_steps\":"<<vm.steps()<<",\"ticks\":"<<ticks
        <<",\"tick_budget_exhausted\":"<<(tick_exhausted?"true":"false")<<",\"instruction_budget_exhausted\":"<<(instruction_exhausted?"true":"false")<<",\"stack_depth\":"<<vm.stack().size()<<",\"rng_cycle_requests\":"<<rng_requests
        <<",\"rng_backend\":\"FreeSO xorshift64star reference profile\",\"rng_seed_initial\":"<<seed.value_or(1)<<",\"rng_seed_final\":"<<random.seed()
        <<",\"rng_seed_source\":"<<json_bytes(seed?"explicit host fixture; original saved seed unavailable":"host default fixture1; original saved seed unavailable")
        <<",\"random_reads\":[";
    for(std::size_t n=0;n<random_reads.size();++n){if(n)std::cout<<',';const auto& value=random_reads[n];
        std::cout<<"{\"owner\":"<<value.context.code_owner<<",\"range\":"<<value.range<<",\"result\":"<<value.result
            <<",\"destination_scope\":"<<static_cast<unsigned>(value.destination_scope)<<",\"destination_index\":"<<value.destination_data
            <<",\"seed_before\":"<<value.seed_before<<",\"seed_after\":"<<value.seed_after<<'}';}
    std::cout<<"],\"queue_size\":"<<queue.size()<<",\"queue_active\":"<<(queue.active()?"true":"false")<<",\"action_incomplete\":"<<(action_incomplete?"true":"false")<<",\"action\":";
    if(action)std::cout<<"{\"archive\":"<<json_bytes(action->archive)<<",\"entry\":"<<json_bytes(action->entry)<<",\"objd_id\":"<<*action_def<<",\"guid\":"<<action_data->binding->guid<<",\"owner\":2,\"routine\":"<<*action_routine<<'}';else std::cout<<"null";
    std::cout<<",\"completion\":";
    if(queue.completion()){const auto& c=*queue.completion();std::cout<<"{\"uid\":"<<c.uid<<",\"ticket\":"<<c.ticket<<",\"tick\":"<<c.tick<<",\"result\":"<<(c.result?"true":"false")<<'}';}else std::cout<<"null";
    std::cout<<",\"tuning_profile\":\"explicit BCON only; no OTF or entity replacement backend; missing remains unsupported\",\"tuning_reads\":[";
    for(std::size_t n=0;n<tuning_reads.size();++n){if(n)std::cout<<',';const auto& v=tuning_reads[n];
        const auto scope=v.address.scope==odh::RoutineScope::global?"global":v.address.scope==odh::RoutineScope::semiglobal?"semiglobal":"private";
        std::cout<<"{\"owner\":"<<v.owner<<",\"scope\":"<<json_bytes(scope)<<",\"table\":"<<v.address.table<<",\"index\":"<<static_cast<unsigned>(v.address.index)
            <<",\"value\":"<<v.value<<",\"semiglobal_fallback\":"<<(v.semiglobal_fallback?"true":"false")<<'}';
    }
    std::cout<<"],\"lot\":";
    if(lot)std::cout<<"{\"house\":"<<json_bytes(*lot_path)<<",\"selected_id\":"<<*lot_id<<",\"serialized_id\":"<<simi->global_data[10]
        <<",\"simi_version\":"<<simi->version<<",\"serialized_globals\":"<<simi->global_data.size()<<",\"remaining_bytes\":"<<simi->remaining_bytes
        <<",\"profile\":\"SIMI static globals; selected lot overrides10; clock/entity/runtime initialization fields unsupported\",\"zoning\":"<<(zoning_path?json_bytes(*zoning_path):"null")<<'}';
    else std::cout<<"null";
    std::cout<<",\"global_reads\":[";
    for(std::size_t n=0;n<global_reads.size();++n){if(n)std::cout<<',';std::cout<<"{\"index\":"<<global_reads[n].first<<",\"value\":"<<global_reads[n].second<<'}';}
    std::cout<<"],\"zoning_reads\":[";
    for(std::size_t n=0;n<zoning_reads.size();++n){if(n)std::cout<<',';const auto& value=zoning_reads[n];
        std::cout<<"{\"input\":"<<value.input<<",\"value\":"<<value.value<<",\"fallback\":"<<(value.fallback?"true":"false")<<'}';}
    std::cout<<"],\"neighborhood\":";
    if(neighbors)std::cout<<"{\"file\":"<<json_bytes(*neighborhood_path)<<",\"version\":"<<neighbors->source().version
        <<",\"entries\":"<<neighbors->source().entries.size()<<",\"active\":"<<neighbors->active_count()<<",\"remaining_bytes\":"<<neighbors->source().remaining_bytes<<'}';
    else std::cout<<"null";
    std::cout<<",\"neighbor_next\":[";
    for(std::size_t n=0;n<neighbor_next.size();++n){if(n)std::cout<<',';const auto& value=neighbor_next[n];
        std::cout<<"{\"current\":"<<value.current<<",\"result\":"<<value.result<<",\"success\":"<<(value.success?"true":"false")<<'}';}
    std::cout<<"],\"neighbor_reads\":[";
    for(std::size_t n=0;n<neighbor_reads.size();++n){if(n)std::cout<<',';const auto& value=neighbor_reads[n];
        const auto source=value.source==odh::NeighborDataSource::serialized?"serialized":value.source==odh::NeighborDataSource::legacy_zero_extension?"legacy_zero_extension":
            value.source==odh::NeighborDataSource::missing_neighbor_zero?"missing_neighbor_reference_zero":"absent_person_data_reference_zero";
        std::cout<<"{\"neighbor\":"<<value.neighbor<<",\"index\":"<<value.index<<",\"value\":"<<value.value<<",\"source\":"<<json_bytes(source)<<'}';}
    std::cout<<']';
    const auto* actor=world.find(1);
    std::cout<<",\"constructor_profile\":\"FreeSO 4c6b3e8 subset; PersonData101/MotiveData16/Contained3; raw ObjectData0..78 except derived getters; World identity/container/slot only\",\"flags_initial\":"<<flags.value_or(256)
        <<",\"age_initial\":"<<age.value_or(0)<<",\"actor_state\":{\"flags\":"<<actor->object_words.at(8)<<",\"age\":"<<actor->person_data[58]
        <<",\"autonomy\":"<<actor->person_data[36]<<",\"weight\":"<<actor->object_words.at(27)<<",\"support_strength\":"<<actor->object_words.at(28)
        <<",\"rotation_notches\":"<<actor->object_words.at(23)<<",\"flag_field2\":"<<actor->object_words.at(40)<<",\"room_placement\":"<<actor->object_words.at(30)<<",\"hunger\":"<<actor->motives[7]<<",\"person_data\":[";
    for(std::size_t n=0;n<actor->person_data.size();++n){if(n)std::cout<<',';std::cout<<actor->person_data[n];}
    std::cout<<"],\"motives\":[";for(std::size_t n=0;n<actor->motives.size();++n){if(n)std::cout<<',';std::cout<<actor->motives[n];}std::cout<<"]}";
    std::cout<<",\"stack\":[";for(std::size_t n=0;n<vm.stack().size();++n){if(n)std::cout<<',';const auto& f=vm.stack()[n];
        std::cout<<"{\"owner\":"<<f.context.code_owner<<",\"routine\":"<<f.key.id<<",\"pc\":"<<f.pc<<",\"caller\":"<<f.context.caller<<",\"callee\":"<<f.context.callee<<",\"stack_object\":"<<f.context.stack_object<<'}';}
    std::cout<<"]}\n";
    return host_failed||status==odh::VmStatus::fault||status==odh::VmStatus::budget_exhausted||tick_exhausted||instruction_exhausted||action_incomplete?3:0;
 }catch(const std::exception& e){std::cerr<<"odh-person: "<<e.what()<<'\n';return 2;}
}
