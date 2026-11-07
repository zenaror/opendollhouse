#include "saved_interactions.hpp"
#include "odh/neighbor_info.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void put(std::string& b,std::uint32_t v,unsigned n,bool big=false){for(unsigned i=0;i<n;++i)b+=static_cast<char>((v>>((big?n-1-i:i)*8))&255);}
std::string chunk(const std::string& type,unsigned id,const std::string& payload){
    std::string b(76,'\0');b.replace(0,4,type);for(unsigned i=0;i<4;++i)b[4+i]=static_cast<char>((76+payload.size())>>((3-i)*8));
    b[8]=static_cast<char>(id>>8);b[9]=static_cast<char>(id);return b+payload;
}
std::string iff(const std::string& payload){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');return b+payload;}
std::string ttab(){std::string b;put(b,1,2);put(b,8,2);put(b,0x1001,2);put(b,0x1002,2);put(b,0,4);put(b,0,4);put(b,0,4);put(b,4,4);put(b,0x3f800000,4);put(b,50,4);put(b,0xffffffff,4);return b;}
std::string labels(){std::string b;put(b,0,2);put(b,1,2);b+=char(3);b+="Sit";return b;}
std::string localized_labels(){std::string b;put(b,65533,2);put(b,2,2);
    b.push_back(1);b+=std::string("Caf\xe9\0\0",6);b.push_back(14);b+=std::string("Sentar\0\0",8);return b;}
std::string test_bhav(unsigned opcode=2){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,opcode,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    if(opcode==2){b.push_back(0);b.push_back(0);b.push_back(11);b.push_back(0);b.push_back(0);b.push_back(2);b.push_back(16);b.push_back(3);}
    else b.append(8,'\0');return b;}
std::string budget_test_bhav(std::uint16_t amount,std::uint8_t flags){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,25,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    b.push_back(0);b.push_back(7);put(b,amount,2);b.push_back(static_cast<char>(flags));b.push_back(0);b.push_back(3);b.push_back(0);return b;}
std::string tuning_test_bhav(std::uint16_t query=261,std::uint16_t expected=0){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,2,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    put(b,query,2);put(b,expected,2);b.push_back(0);b.push_back(2);b.push_back(26);b.push_back(7);return b;}
std::string expression_bhav(const std::vector<std::array<std::uint8_t,8>>& expressions){
    std::string b;put(b,0x8002,2);put(b,static_cast<std::uint32_t>(expressions.size()),2);
    b.push_back(0);b.push_back(4);b.append(6,'\0');
    for(std::size_t i=0;i<expressions.size();++i){
        put(b,2,2);b.push_back(static_cast<char>(i+1<expressions.size()?i+1:254));
        b.push_back(static_cast<char>(255));
        for(const auto byte:expressions[i])b.push_back(static_cast<char>(byte));
    }
    return b;
}
std::string category_test_bhav(std::uint16_t expected){
    std::string b;put(b,0x8002,2);put(b,3,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,2,2);b.push_back(1);b.push_back(static_cast<char>(255));
    // Start category enumeration before the first entity.
    for(const auto byte:std::array<std::uint8_t,8>{0,0,0,0,0,5,10,7})b.push_back(static_cast<char>(byte));
    put(b,31,2);b.push_back(2);b.push_back(static_cast<char>(255));
    for(const auto byte:std::array<std::uint8_t,8>{0,0,0,0,6,0,0,0})b.push_back(static_cast<char>(byte));
    put(b,2,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    put(b,0,2);put(b,expected,2);b.push_back(0);b.push_back(2);b.push_back(10);b.push_back(7);
    return b;
}
std::string object_type_test_bhav(std::uint32_t guid){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,32,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    put(b,guid,4);put(b,0,2);b.push_back(10);b.push_back(0);return b;}
std::string call_bhav(unsigned callee){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,callee,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));b.append(8,'\0');return b;}
std::string write_bhav(){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,2,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    b.push_back(0);b.push_back(0);b.push_back(1);b.push_back(0);b.push_back(0);b.push_back(5);b.push_back(3);b.push_back(7);return b;}
std::string inventory_test_bhav(std::uint32_t guid){std::string b;put(b,0x8002,2);put(b,2,2);b.push_back(0);b.push_back(0);b.append(6,'\0');
    put(b,2,2);b.push_back(1);b.push_back(static_cast<char>(255));
    b.push_back(20);b.push_back(0);b.push_back(8);b.push_back(0);b.push_back(0);b.push_back(8);b.push_back(6);b.push_back(7);
    put(b,51,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    b.push_back(3);b.push_back(7);b.push_back(0);b.push_back(0);put(b,guid,4);return b;}
std::string inventory_effect_bhav(std::uint32_t guid){
    std::string b;put(b,0x8002,2);put(b,2,2);b.push_back(0);b.push_back(0);b.append(6,'\0');
    for(const std::uint8_t mode:{0,3}){
        put(b,51,2);b.push_back(static_cast<char>(mode==0?1:254));b.push_back(static_cast<char>(255));
        b.push_back(static_cast<char>(mode));b.push_back(7);b.push_back(0);b.push_back(0);put(b,guid,4);
    }
    return b;
}
std::string call_global_bhav(unsigned callee){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(0);b.append(6,'\0');
    put(b,callee,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));b.append(8,'\0');return b;}
std::string constant_true_bhav(){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(0);b.append(6,'\0');
    put(b,2,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    b.push_back(1);b.push_back(0);b.push_back(1);b.push_back(0);b.push_back(0);b.push_back(2);b.push_back(7);b.push_back(7);return b;}
std::string zone_test_global_bhav(){std::string b;put(b,0x8002,2);put(b,3,2);b.push_back(0);b.push_back(0);b.append(6,'\0');
    put(b,2,2);b.push_back(1);b.push_back(static_cast<char>(253));
    b.push_back(0);b.push_back(0);b.push_back(10);b.push_back(0);b.push_back(0);b.push_back(5);b.push_back(8);b.push_back(6);
    put(b,1,2);b.push_back(2);b.push_back(static_cast<char>(253));b.push_back(28);b.append(7,'\0');
    const auto finish=constant_true_bhav();put(b,2,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));
    b+=finish.substr(16,8);return b;}
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));if(!f)throw std::runtime_error("fixture write failed");}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){
    std::string bytes="FAR!byAZ";put(bytes,1,4);put(bytes,0,4);
    std::vector<std::uint32_t> offsets;
    for(const auto& entry:entries){offsets.push_back(static_cast<std::uint32_t>(bytes.size()));bytes+=entry.second;}
    const auto manifest=static_cast<std::uint32_t>(bytes.size());
    for(unsigned i=0;i<4;++i)bytes[12+i]=static_cast<char>(manifest>>(i*8));
    put(bytes,static_cast<std::uint32_t>(entries.size()),4);
    for(std::size_t i=0;i<entries.size();++i){const auto& entry=entries[i];
        put(bytes,static_cast<std::uint32_t>(entry.second.size()),4);put(bytes,static_cast<std::uint32_t>(entry.second.size()),4);
        put(bytes,offsets[i],4);put(bytes,static_cast<std::uint32_t>(entry.first.size()),4);bytes+=entry.first;}
    return bytes;
}
void source_error_menu_matrix(const std::filesystem::path& root){
 const auto path=root/"source-error-menu.iff";write(path,iff(chunk("BHAV",4098,test_bhav(98))));
 odh::ObjectDefinition object{};object.guid=123;object.tree_table_id=128;
 const odh::DefinitionCandidate definition{path.string(),"",7,object};
 odh::World world;check(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object)&&
    world.set_position(1,{16,16,1,0})&&world.define_type(2,123),"menu ERROR fixture has real caller/target identities");
 for(unsigned mode=0;mode<5;++mode){
  unsigned called=0;
  const odh::VmPrimitiveFactory factory=[&](const odh::VmMemory&){return odh::VmPrimitive{
   [&](const auto&,const auto&,const auto&,const auto&)->odh::PrimitiveOutcome{
    ++called;if(mode==4)throw std::runtime_error("authored absent object");
    return {mode==0?odh::PrimitiveDisposition::source_error:mode==1?odh::PrimitiveDisposition::unsupported:
      mode==2?odh::PrimitiveDisposition::wait:odh::PrimitiveDisposition::continue_execution,"authored absent object"};
   }};};
  const auto result=evaluate_saved_test_functions(definition,world,1,2,{4098},16,16,{0,0,0,0},nullptr,{},nullptr,nullptr,
      {},nullptr,nullptr,nullptr,std::nullopt,nullptr,false,{},factory);
  check(result.size()==1&&called>0,"menu ERROR matrix executes actual TestFunction");
  check(result[0].state==(mode==0?SavedTestFunctionResult::State::ineligible:SavedTestFunctionResult::State::unsupported),
    "only known source ERROR excludes menu row; unknown implementation, wait, work bound and exception stay unsupported");
 }
}
void explicit_named_check_resources(const std::filesystem::path& root){
    const auto path=root/"named-check-resource-context.iff";
    write(path,iff(chunk("BHAV",4098,test_bhav(28))));
    odh::ObjectDefinition object{};object.guid=123;object.tree_table_id=128;
    const odh::DefinitionCandidate definition{path.string(),"",7,object};
    odh::World world;check(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object)&&
        world.set_position(1,{16,16,1,0})&&world.define_type(2,123),"create actual menu caller/target resource identities");
    const auto named=odh::named_routine_key(42,false);
    const auto bank=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{named,
        {0x8002,{{2,254,255,{0,0,9,0,0,2,8,7}}},{},odh::Bhav::Metadata{0,0,0,0}}}});
    unsigned explicit_lookups=0,implicit_lookups=0;
    odh::Vm owner({{odh::routine_key(4096),{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,0,0,0}}}},
        world.vm_memory(),{},[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    check(owner.set_owner_resolver([&](auto)->std::shared_ptr<const odh::VmProgram>{++implicit_lookups;return bank;}),
        "real Thread may own a provider independently of its Temp seed");
    owner.start(odh::routine_key(4096),{1,1,1},{},true);check(owner.run(1)==odh::VmStatus::waiting,"park actual menu caller Main");
    owner.temps()[0]=9;const auto before=owner.snapshot();
    const odh::VmPrimitiveFactory factory=[&](const odh::VmMemory&){return odh::VmPrimitive{
        [&](const odh::VmFrame& frame,const odh::Instruction& instruction,const auto&,const odh::VmDispatch& dispatch)->odh::PrimitiveOutcome{
            if(instruction.opcode!=28||!dispatch.origin||!dispatch.origin->matches_active_query(frame,instruction,dispatch))return {};
            auto context=frame.context;context.code_owner=77;
            const auto result=const_cast<odh::Vm*>(dispatch.origin)->run_named_in_stack(named,context,{},16);
            if(!result.started||(result.status!=odh::VmStatus::returned_true&&result.status!=odh::VmStatus::returned_false))
                return {odh::PrimitiveDisposition::unsupported,"fixture named lookup: "+result.diagnostic};
            return {result.status==odh::VmStatus::returned_true?odh::PrimitiveDisposition::goto_true:odh::PrimitiveDisposition::goto_false,{}};
        }};};
    const auto evaluate=[&](const odh::Vm::OwnerResolver& resources){return evaluate_saved_test_functions(definition,world,1,2,{4098},128,64,
        {0,0,0,0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,nullptr,false,{},factory,&owner,false,{},nullptr,nullptr,resources);};
    const auto absent=evaluate({});check(absent.size()==1&&absent[0].state==SavedTestFunctionResult::State::unsupported&&implicit_lookups==0,
        "TestFunction never infers resource authority from a real Thread used only as the register seed");
    const odh::Vm::OwnerResolver resources=[&](odh::CodeOwner id)->std::shared_ptr<const odh::VmProgram>{
        ++explicit_lookups;check(id==77,"named lookup requests exact separate immutable owner namespace");return bank;};
    const auto allowed=evaluate(resources);check(allowed.size()==1&&allowed[0].state==SavedTestFunctionResult::State::eligible&&explicit_lookups==1,
        "explicit resource context admits and executes a named condition in the real shared TestFunction consumer");
    owner.temps()[0]=8;const auto denied=evaluate(resources);
    check(denied.size()==1&&denied[0].state==SavedTestFunctionResult::State::ineligible&&explicit_lookups==2,
        "named condition returns its authored false result instead of manufacturing eligibility");
    check(owner.pending()&&owner.pending()->ticket==before.pending->ticket&&owner.steps()==before.steps&&
        owner.run_generation()==before.run_generation&&odh::vm_frame_equal(owner.stack().front(),before.stack.front())&&
        !owner.shared_program_for_owner(77)&&implicit_lookups==0,
        "each menu clone keeps lazy resource admissions local and preserves the real Main continuation");
}
void named_dynamic_menu_owner(const std::filesystem::path& root){
    const auto phone=root/"phone-menu-owner.iff",plugin=root/"plugin-menu-owner.iff";
    // Match PhoneGlobals: assign the plugin StackObject BEFORE RunTreeByName,
    // then destination2 calls a child without changing participant references.
    std::string caller;put(caller,0x8002,2);put(caller,2,2);caller.push_back(0);caller.push_back(4);caller.append(6,'\0');
    put(caller,2,2);caller.push_back(1);caller.push_back(static_cast<char>(253));
    for(const auto byte:{0,0,3,0,0,5,10,7})caller.push_back(static_cast<char>(byte));
    put(caller,28,2);caller.push_back(static_cast<char>(254));caller.push_back(static_cast<char>(255));
    for(const auto byte:{0,0,0,0,0,2,0,0})caller.push_back(static_cast<char>(byte));
    write(phone,iff(chunk("BHAV",4098,caller)));
    std::string text;put(text,65533,2);put(text,1,2);text.push_back(1);text+="Travel/Go Downtown";text.append(2,'\0');
    write(plugin,iff(chunk("STR#",302,text)));
    odh::ObjectDefinition object{};object.guid=123;
    const odh::DefinitionCandidate definition{phone.string(),"",7,object};
    odh::World world;check(world.create(1,odh::EntityKind::avatar)&&world.set_position(1,{16,16,1,0})&&
        world.create(2,odh::EntityKind::object)&&world.define_type(2,123)&&
        world.create(3,odh::EntityKind::object)&&world.define_type(3,456),"named menu has distinct actor, phone and plugin identities");
    const auto key=odh::named_routine_key(4141,false);
    auto program=std::make_shared<const odh::VmProgram>(odh::RoutineSet{{key,
        {0x8002,{{2,1,255,{135,0,9,0,0,2,26,7}},
                  {50,254,255,{46,1,0,0,1,0,0,0}}},{},odh::Bhav::Metadata{0,4,0,0}}}});
    const odh::Vm::OwnerResolver resources=[&](odh::CodeOwner owner){return owner==77?program:std::shared_ptr<const odh::VmProgram>{};};
    const odh::VmPrimitiveFactory factory=[&](const odh::VmMemory&){return odh::VmPrimitive{
        [&](const odh::VmFrame& frame,const odh::Instruction& ins,const auto&,const odh::VmDispatch& dispatch)->odh::PrimitiveOutcome{
            if(ins.opcode!=28)return {};
            auto* vm=const_cast<odh::Vm*>(dispatch.origin);
            if(!vm||!vm->prepare_primitive_owner(frame,ins,dispatch,77))return {odh::PrimitiveDisposition::unsupported,"plugin bank unavailable"};
            auto context=frame.context;context.code_owner=77;
            return {odh::PrimitiveDisposition::call,{},{},odh::VmCall{key,context,{0,0,0,0}}};
        }};};
    bool available=true;unsigned lookups=0;
    const SavedMenuStringResolver strings=[&](const odh::VmFrame& frame,const odh::Instruction& ins)->SavedMenuStringResult{
        ++lookups;check(frame.context.code_owner==77&&frame.context.callee==2&&frame.context.stack_object==3&&
            ins.operands[2]==0,"private dynamic name follows plugin CodeOwner independently of Callee and Param0");
        if(!available)return {false,{},"plugin STR owner unavailable"};
        std::optional<odh::StringTable> table;
        with_iff_file(plugin.string(),[&](const auto& view){odh::iff_chunks(view,[&](const auto& c){
            if(c.type=="STR#"&&c.id==302)table=odh::read_strings(c.payload);});});
        check(table.has_value(),"exact plugin private STR exists");
        const auto* entry=odh::string_in_ts1_language_set(*table,1,0);
        return {true,entry?std::optional<std::string>{entry->value}:std::nullopt,{}};
    };
    odh::TuningResources selected_tuning;
    selected_tuning.owners[2].private_tables[4097]={0,std::vector<std::uint16_t>(8,444),0};
    bool tuning_available=true;std::int16_t plugin_value=9;unsigned tuning_reads=0;
    odh::VmMemory host_memory;host_memory.read=[&](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t index)
        ->std::optional<std::int16_t>{
        check(frame.context.code_owner==77&&scope==26&&index==135,
            "named private BCON read requests plugin owner rather than selected phone or register donor");
        ++tuning_reads;return tuning_available?std::optional<std::int16_t>{plugin_value}:std::nullopt;
    };
    const auto evaluate=[&](std::uint64_t budget){return evaluate_saved_test_functions(definition,world,1,2,{4098},budget,budget,
        {0,0,0,0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,&selected_tuning,false,
        strings,factory,nullptr,false,{},&host_memory,nullptr,resources);};
    const auto result=evaluate(128);
    if(result.front().state!=SavedTestFunctionResult::State::eligible||result.front().variants.size()!=1)
        std::cerr<<"named dynamic menu status="<<static_cast<unsigned>(result.front().state)
            <<" diagnostic="<<result.front().diagnostic<<" variants="<<result.front().variants.size()
            <<" string_lookups="<<lookups<<'\n';
    check(result.front().state==SavedTestFunctionResult::State::eligible&&result.front().variants.size()==1&&
        result.front().variants.front().name==std::optional<std::string>{"Travel/Go Downtown"}&&
        result.front().variants.front().param0==3&&lookups==1&&tuning_reads==1,
        "actual named plugin check publishes its private-resource name and plugin Param0 through the shared menu consumer");
    available=false;const auto missing=evaluate(128);
    check(missing.front().state==SavedTestFunctionResult::State::unsupported&&missing.front().variants.empty(),
        "missing plugin authority cannot use selected phone strings or manufacture eligibility");
    available=true;tuning_available=false;const auto unknown_tuning=evaluate(128);
    check(unknown_tuning.front().state==SavedTestFunctionResult::State::unsupported&&unknown_tuning.front().variants.empty()&&
        unknown_tuning.front().diagnostic.find("unsupported read scope 26 index 135")!=std::string::npos,
        "unknown plugin tuning cannot borrow selected phone constants or silently become zero");
    tuning_available=true;plugin_value=0;const auto zero=evaluate(128);
    check(zero.front().state==SavedTestFunctionResult::State::ineligible&&zero.front().variants.empty(),
        "authenticated zero constant follows the authored false branch rather than missing-resource fallback");
    const auto bounded=evaluate(1);check(bounded.front().state==SavedTestFunctionResult::State::unsupported,
        "cross-owner dynamic names do not bypass instruction budgets");
}
void full_category_check_budget(const std::filesystem::path& root){
    const auto path=root/"full-category-check-budget.iff";
    odh::ObjectDefinition definition{};definition.guid=123;
    const odh::DefinitionCandidate candidate{path.string(),"",7,definition};
    odh::World world;check(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object)&&
        world.set_position(1,{16,16,1,0})&&world.define_type(2,definition.guid),
        "create visible actual category-query caller and bind the target's exact resource GUID");
    for(std::int16_t id=3;id<74;++id)check(world.create(id,odh::EntityKind::object)&&world.declare_object_word(id,59,1),
        "admit full71-controller category cohort with real World identities");
    const auto program=[](bool cycle){
        std::string bytes;put(bytes,0x8002,2);put(bytes,5,2);bytes.push_back(0);bytes.push_back(4);bytes.append(6,'\0');
        const auto emit=[&](unsigned opcode,unsigned yes,unsigned no,const std::array<std::uint8_t,8>& operands){
            put(bytes,opcode,2);bytes.push_back(static_cast<char>(yes));bytes.push_back(static_cast<char>(no));
            for(const auto value:operands)bytes.push_back(static_cast<char>(value));};
        emit(2,1,255,{0,0,0,0,0,5,8,7}); // Temp0 = 0
        emit(2,2,255,{0,0,0,0,0,5,10,7}); // StackObject = nil
        emit(31,3,4,{0,0,0,0,134,10,0,0}); // next Category Param0, strict increasing World cursor
        emit(2,cycle?1:2,255,{0,0,1,0,0,3,8,7}); // count; cyclic case explicitly restarts its cursor
        emit(2,254,255,{0,0,71,0,0,2,8,7}); // require complete enumeration, never approve a prefix
        return bytes;
    };
    odh::Vm owner({{odh::routine_key(4096),{0x8002,{{0,254,255,{}}},{},odh::Bhav::Metadata{0,0,0,0}}}},
        world.vm_memory(),{},[](const auto&,const auto&,const auto&,const auto&){return odh::PrimitiveOutcome{odh::PrimitiveDisposition::wait,{}};});
    owner.start(odh::routine_key(4096),{1,1,1},{},true);check(owner.run(1)==odh::VmStatus::waiting,"park real query caller continuation");
    const auto before=owner.snapshot();
    const auto evaluate=[&](std::vector<std::uint16_t> tests,std::uint64_t total,std::uint64_t per){return evaluate_saved_test_functions(
        candidate,world,1,2,tests,total,per,{1,0,0,0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,
        nullptr,false,{},{},&owner,true);};
    const auto detail=[&](const SavedTestFunctionResult& result){return " state="+std::to_string(static_cast<int>(result.state))+
        " steps="+std::to_string(result.steps)+" temp0="+std::to_string(owner.temps()[0])+" diagnostic="+result.diagnostic;};
    write(path,iff(chunk("BHAV",4098,program(false))));
    const auto old=evaluate({4098},1000,128);
    check(old[0].state==SavedTestFunctionResult::State::unsupported&&old[0].steps==128&&owner.temps()[0]<71,
        ("the former128 instruction slice truncates a real finite increasing category enumeration"+detail(old[0])).c_str());
    const auto complete=evaluate({4098},saved_menu_total_budget,saved_menu_check_budget);
    check(complete[0].state==SavedTestFunctionResult::State::eligible&&complete[0].steps==146&&owner.temps()[0]==71,
        ("source-based live menu slice completes all71 distinct category entities and the authored final count condition"+detail(complete[0])).c_str());
    const auto aggregate=evaluate({4098,4098},200,saved_menu_check_budget);
    check(aggregate[0].state==SavedTestFunctionResult::State::eligible&&aggregate[1].state==SavedTestFunctionResult::State::unsupported&&
        aggregate[0].steps+aggregate[1].steps==200&&aggregate[1].diagnostic.find("budget=54")!=std::string::npos,
        ("larger per-check source limit never expands the independent aggregate instruction ceiling"+detail(aggregate[1])).c_str());
    write(path,iff(chunk("BHAV",4098,program(true))));
    const auto cyclic=evaluate({4098},64,saved_menu_check_budget);
    check(cyclic[0].state==SavedTestFunctionResult::State::unsupported&&cyclic[0].steps==64&&
        cyclic[0].diagnostic.find("owner 2 scope private")!=std::string::npos&&
        cyclic[0].diagnostic.find("cursor=")!=std::string::npos&&cyclic[0].diagnostic.find("category_cursor_rewinds=")!=std::string::npos,
        ("a truly cyclic category restart remains bounded and unsupported with owner/scope/cursor diagnostics"+detail(cyclic[0])).c_str());
    check(owner.pending()&&owner.pending()->ticket==before.pending->ticket&&owner.steps()==before.steps&&
        owner.run_generation()==before.run_generation&&odh::vm_frame_equal(owner.stack().front(),before.stack.front()),
        "finite and exhausted menu checks preserve the real Main continuation independently of shared Temp effects");
}
void semiglobal_table_discovery(const std::filesystem::path& root){
    using State=SavedObjectInteractions::State;using Origin=SavedObjectInteractions::TableOrigin;
    const auto source=root/"semiglobal-source.iff",archive=root/"semiglobal-global.far";
    odh::ObjectDefinition object{};object.guid=321;object.tree_table_id=128;
    const odh::DefinitionCandidate definition{source.string(),"",42,object};
    const odh::DefinitionIndex index({definition});
    const auto glob=[](const std::string& name){return chunk("GLOB",128,std::string(1,static_cast<char>(name.size()))+name);};
    auto semi_table=ttab();semi_table[4]=1;semi_table[5]=0x20;
    const auto semi=iff(chunk("TTAB",128,semi_table)+chunk("TTAs",128,localized_labels())+
        chunk("TTAB",131,semi_table)+chunk("TTAs",131,localized_labels()));
    const auto write_archive=[&](const std::string& exact){write(archive,far({
        {"Unrelated.iff",iff(chunk("TTAB",128,ttab())+chunk("TTAs",128,labels()))},
        {"PersonGlobals.iff",exact}}));};
    write_archive(semi);write(source,iff(glob("PersonGlobals")+chunk("TTAs",128,labels())));
    check(saved_object_interactions(definition,index,14).state==State::missing_ttab,
        "private-only API never guesses a semiglobal archive");
    const auto fallback=saved_object_interactions(definition,index,14,archive.string());
    check(fallback.state==State::found&&fallback.table&&fallback.table->interactions[0].action==0x2001&&
        fallback.source==source.string()&&fallback.asset.empty()&&fallback.table_source==archive.string()&&
        fallback.table_asset=="PersonGlobals.iff"&&fallback.table_origin==Origin::semiglobal_resource&&fallback.table_id==128,
        "missing private TTAB falls back to exact GLOB bank while retaining definition/code provenance");
    check(fallback.row_labels[0]=="Sentar"&&fallback.row_label_languages[0]==14,
        "semiglobal table uses its own requested-language TTAs rather than private labels");
    const auto english=saved_object_interactions(definition,index,5,archive.string());
    check(english.row_labels[0]==std::string("Caf\xe9")&&english.row_label_languages[0]==1,
        "semiglobal labels preserve English fallback encoding provenance");
    write(source,iff(glob("PersonGlobals")+chunk("TTAB",128,ttab())));
    const auto local=saved_object_interactions(definition,index,14,archive.string());
    check(local.table->interactions[0].action==0x1001&&local.table_source==source.string()&&
        local.table_asset.empty()&&local.table_origin==Origin::private_resource&&!local.row_labels[0],
        "present private table wins and missing private labels do not borrow semiglobal TTAs");
    write(source,iff(glob("PersonGlobals")));
    odh::ObjectDefinition part=object;part.guid=322;part.tree_table_id=0xffff;part.master_id=11;part.sub_index=0;
    odh::ObjectDefinition master=object;master.guid=323;master.tree_table_id=131;master.master_id=11;master.sub_index=-1;
    const odh::DefinitionCandidate child{source.string(),"",43,part},owner{source.string(),"",44,master};
    const odh::DefinitionIndex group({child,owner});
    const auto inherited=saved_object_interactions(child,group,14,archive.string());
    check(inherited.state==State::found&&inherited.table_id==131&&inherited.table_origin==Origin::semiglobal_resource&&
        inherited.source==child.source&&inherited.asset==child.asset&&inherited.table_asset=="PersonGlobals.iff"&&
        inherited.row_labels[0]=="Sentar","master table ID is resolved before exact semiglobal fallback without replacing child identity");
    write(source,iff(glob("PersonGlobals")+chunk("TTAB",131,ttab())));
    const auto master_local=saved_object_interactions(child,group,14,archive.string());
    check(master_local.table_origin==Origin::master_resource&&master_local.table_source==source.string()&&
        master_local.table->interactions[0].action==0x1001,"same-resource master TTAB retains priority over semiglobal table");
    auto explicit_part=child;explicit_part.definition.tree_table_id=128;
    const odh::DefinitionIndex explicit_group({explicit_part,owner});
    const auto child_semi=saved_object_interactions(explicit_part,explicit_group,14,archive.string());
    check(child_semi.table_id==128&&child_semi.table_origin==Origin::semiglobal_resource&&
        child_semi.table->interactions[0].action==0x2001,
        "selected child's semiglobal table precedes master private inheritance as in VMEntity.UseTreeTableOf");
    write(source,iff(glob("PersonGlobals")+chunk("TTAB",128,ttab())+chunk("TTAB",131,semi_table)));
    const auto child_local=saved_object_interactions(explicit_part,explicit_group,14,archive.string());
    check(child_local.table_id==128&&child_local.table_origin==Origin::private_resource&&
        child_local.table->interactions[0].action==0x1001,"selected child's private table is never replaced by its master");
    write(source,iff(glob("PersonGlobals")+chunk("TTAB",131,ttab())));
    write_archive(iff(chunk("TTAB",65535,semi_table)+chunk("TTAs",65535,localized_labels())));
    const auto child_sentinel_table=saved_object_interactions(child,group,14,archive.string());
    check(child_sentinel_table.table_id==65535&&child_sentinel_table.table_origin==Origin::semiglobal_resource&&
        child_sentinel_table.row_labels[0]=="Sentar",
        "a real child table at its authored ID wins before master fallback even for the 0xffff marker");
    write_archive(semi);
    auto missing_part=child;missing_part.definition.tree_table_id=130;
    const odh::DefinitionIndex missing_group({missing_part,owner});
    const auto missing_local=saved_object_interactions(missing_part,missing_group,14,archive.string());
    check(missing_local.table_id==131&&missing_local.table_origin==Origin::master_resource&&
        missing_local.source==missing_part.source,"missing non-sentinel child ID inherits only its unique same-resource master");
    write(source,iff(glob("PersonGlobals")));
    const auto missing_semi=saved_object_interactions(missing_part,missing_group,14,archive.string());
    check(missing_semi.table_id==131&&missing_semi.table_origin==Origin::semiglobal_resource&&
        missing_semi.table_asset=="PersonGlobals.iff"&&missing_semi.row_labels[0]=="Sentar",
        "master's exact semiglobal fallback also applies to a missing non-sentinel child ID");
    auto second_master=owner;second_master.definition.guid=324;second_master.chunk_id=45;
    const odh::DefinitionIndex ambiguous_group({missing_part,owner,second_master});
    check(saved_object_interactions(missing_part,ambiguous_group,14,archive.string()).state==State::missing_ttab,
        "ambiguous master cannot authorize another TTAB ID in a shared semiglobal bank");
    auto zero_part=missing_part;zero_part.definition.tree_table_id=0;
    const odh::DefinitionIndex zero_group({zero_part,owner});
    check(saved_object_interactions(zero_part,zero_group,14,(root/"nonexistent.far").string()).state==State::no_tree_table_id,
        "explicit no-table zero ID retains its bounded profile without implicit master or archive access");
    const auto rejects=[&](const auto& run,const char* text){bool rejected=false;
        try{run();}catch(const odh::FormatError&){rejected=true;}check(rejected,text);};
    write(source,iff(glob("PersonGlobals")+glob("PersonGlobals")));
    rejects([&]{(void)saved_object_interactions(definition,index,1,archive.string());},"duplicate identical GLOB bindings cannot authorize semiglobal fallback");
    write(source,iff(glob("PersonGlobals")+glob("Unrelated")));
    rejects([&]{(void)saved_object_interactions(definition,index,1,archive.string());},"conflicting GLOB bindings reject instead of choosing first table");
    write(source,iff(chunk("GLOB",128,std::string(1,char(10))+"x")));
    rejects([&]{(void)saved_object_interactions(definition,index,1,archive.string());},"malformed declared GLOB cannot become absent link");
    write(source,iff(glob("../PersonGlobals")));
    rejects([&]{(void)saved_object_interactions(definition,index,1,archive.string());},"GLOB traversal cannot select a different table resource");
    write(source,iff(glob("Missing")));
    rejects([&]{(void)saved_object_interactions(definition,index,1,archive.string());},"missing declared bank rejects without searching unrelated matching TTAB");
    write(source,iff(""));
    check(saved_object_interactions(definition,index,1,archive.string()).state==State::missing_ttab,
        "absent GLOB does not authorize another archive's matching TTAB");
    write(source,iff(glob("PersonGlobals")));write_archive(iff(chunk("TTAs",128,labels())));
    check(saved_object_interactions(definition,index,1,archive.string()).state==State::missing_ttab,
        "loaded semiglobal without requested TTAB remains explicitly missing");
    write_archive(iff(chunk("TTAB",128,semi_table)+chunk("TTAB",128,semi_table)));
    rejects([&]{(void)saved_object_interactions(definition,index,1,archive.string());},"duplicate semiglobal TTAB is rejected");
    write_archive(iff(chunk("TTAB",128,semi_table)+chunk("TTAs",128,labels())+chunk("TTAs",128,labels())));
    rejects([&]{(void)saved_object_interactions(definition,index,1,archive.string());},"duplicate semiglobal TTAs is rejected");
    write(source,iff(glob("PersonGlobals")+chunk("TTAB",128,ttab())+chunk("TTAB",128,ttab())));
    rejects([&]{(void)saved_object_interactions(definition,index,1,archive.string());},"ambiguous private table cannot be hidden by valid semiglobal fallback");
    auto zero=definition;zero.definition.tree_table_id=0;
    check(saved_object_interactions(zero,index,1,(root/"nonexistent.far").string()).state==State::no_tree_table_id,
        "zero table ID keeps no-table rule without accessing a semiglobal archive");
}
}
int main(){try{
    {
        SavedObjectInteractions menu{};
        odh::Interaction row{};row.action=4123;row.label_index=7;
        menu.table=odh::Ttab{odh::TtabStatus::decoded,1,8,false,0,{row}};
        odh::InteractionActionSnapshot action{};action.call.key=odh::routine_key(4123);
        check(saved_queued_interaction_row(menu,action)==0,"unique static row predating interaction numbers");
        menu.table->interactions.push_back(row);menu.row_global={false,true};
        check(!saved_queued_interaction_row(menu,action),"local/global static ambiguity remains unidentified");
        action.interaction_number=7;
        check(saved_queued_interaction_row(menu,action)==0,"declared interaction number selects matching local row");
        action.interaction_number.reset();action.check=odh::routine_key(4124);menu.table->interactions[0].test=4124;
        check(!saved_queued_interaction_row(menu,action),"numberless dynamic action cannot be reconstructed");
    }
    {
        odh::Bhav leaf{0x8002,{{2,255,1,{}},{2,2,255,{}},{50,254,253,{46,1,0,0,3,0,0,0}}}};
        const auto label=saved_leaf_action_string(leaf);
        check(label&&label->first==302&&label->second==2,"one private STR on every successful leaf route");
        leaf.instructions[0].on_true=254;
        check(!saved_leaf_action_string(leaf),"alternate success without a label cannot prove the chosen text");
        leaf.instructions[0].on_true=255;leaf.instructions[1].opcode=4096;
        check(!saved_leaf_action_string(leaf),"nested checks cannot supply a proven leaf label");
        leaf.instructions[1].opcode=2;leaf.instructions[1].on_false=0;
        check(!saved_leaf_action_string(leaf),"cyclic legacy check is not inspected dynamically");
    }
    const auto root=std::filesystem::temp_directory_path()/"odh-saved-interactions-test";
    std::filesystem::create_directories(root);const auto path=root/"object.iff";
    semiglobal_table_discovery(root);
    source_error_menu_matrix(root);
    explicit_named_check_resources(root);
    named_dynamic_menu_owner(root);
    full_category_check_budget(root);
    auto data=iff(chunk("TTAB",128,ttab())+chunk("TTAs",128,labels()));write(path,data);
    odh::ObjectDefinition object{};object.guid=123;object.tree_table_id=128;
    const auto result=saved_object_interactions({path.string(),"",7,object});
    check(result.state==SavedObjectInteractions::State::found&&result.table&&result.table->interactions.size()==1,"owner TTAB discovered");
    check(result.source==path.string()&&result.table_id==128&&result.asset.empty(),"exact owner provenance retained");
    check(result.labels.state==odh::StringTableState::decoded&&result.row_labels.size()==1&&result.row_labels[0]=="Sit","TTAs label mapped by explicit language/index");
    write(path,iff(chunk("TTAB",128,ttab())+chunk("TTAs",128,"broken")+chunk("TTAs",128,"broken")));
    const auto table_only=saved_object_interactions({path.string(),"",7,object},1,nullptr,false);
    check(table_only.table&&table_only.row_labels.empty()&&table_only.labels.state==odh::StringTableState::absent,
        "lookup-only discovery ignores malformed duplicate labels unrelated to its TTAB predicate");
    data=iff(chunk("TTAB",131,ttab()));write(path,data);
    odh::ObjectDefinition part{};part.guid=3586796916U;part.master_id=11;part.sub_index=0;part.tree_table_id=0xffff;
    odh::ObjectDefinition master{};master.guid=2091978777U;master.master_id=11;master.sub_index=-1;master.tree_table_id=131;
    const odh::DefinitionCandidate part_candidate{path.string(),"",16848,part};
    const odh::DefinitionCandidate master_candidate{path.string(),"",16847,master};
    const odh::DefinitionIndex group_index({part_candidate,master_candidate});
    const auto inherited=saved_object_interactions(part_candidate,group_index);
    check(inherited.state==SavedObjectInteractions::State::found&&inherited.table_id==131&&
          inherited.table&&inherited.table->interactions[0].action==0x1001,
          "multitile part inherits its exact same-resource master TTAB");
    check(inherited.source==part_candidate.source&&inherited.asset==part_candidate.asset,
          "inherited TTAB retains selected-part provenance");
    const odh::DefinitionCandidate other_resource_master{path.string(),"Sofas.iff",16847,master};
    const odh::DefinitionIndex split_index({part_candidate,other_resource_master});
    const auto no_cross_resource=saved_object_interactions(part_candidate,split_index);
    check(no_cross_resource.state==SavedObjectInteractions::State::missing_ttab&&
          no_cross_resource.table_id==0xffff,
          "multitile part never inherits a master TTAB from another resource");
    bool unknown_master=false;
    try{(void)saved_object_interactions(part_candidate,split_index,1,{},false,true);}
    catch(const odh::FormatError&){unknown_master=true;}
    check(unknown_master,"strict lookup prewarm distinguishes an unavailable master from proven absent TTAB");
    data=iff(chunk("GLOB",128,std::string(1,'\x0b')+"SofaGlobals"));write(path,data);
    const auto linked_catalog=load_saved_tree_name_catalog(part_candidate);
    check(linked_catalog.semiglobal_name==std::optional<std::string>("SofaGlobals"),
          "GLOB semiglobal link is decoded from the payload, not the IFF chunk label");
    data=iff(chunk("GLOB",128,std::string(1,'\x0b')+"SofaGlobals")+
             chunk("GLOB",129,std::string(1,'\x0b')+"SofaGlobals"));write(path,data);
    const auto duplicate_catalog=load_saved_tree_name_catalog(part_candidate);
    check(duplicate_catalog.semiglobal_bindings==2&&
          duplicate_catalog.semiglobal_name==std::optional<std::string>("SofaGlobals"),
          "shared catalog preserves duplicate GLOB count without disturbing non-SEAT callers");
    bool duplicate_glob_rejected=false;
    try{(void)unique_saved_semiglobal_name(duplicate_catalog);}
    catch(const odh::FormatError&){duplicate_glob_rejected=true;}
    check(duplicate_glob_rejected,"SEAT unique-link accessor rejects duplicate identical GLOBs");
    data=iff(chunk("GLOB",128,std::string(1,'\x0b')+"SofaGlobals")+
             chunk("GLOB",129,std::string(1,'\x0c')+"OtherGlobals"));write(path,data);
    bool conflicting_glob_rejected=false;
    try{(void)load_saved_tree_name_catalog(part_candidate);}catch(const odh::FormatError&){conflicting_glob_rejected=true;}
    check(conflicting_glob_rejected,"conflicting GLOB bindings are rejected as ambiguous");
    data=iff(chunk("TTAB",128,ttab())+chunk("TTAs",128,localized_labels()));write(path,data);
    const auto portuguese=saved_object_interactions({path.string(),"",7,object},14);
    const auto missing_language=saved_object_interactions({path.string(),"",7,object},5);
    check(portuguese.language==14&&portuguese.row_labels[0]=="Sentar","interaction label follows selected Portuguese group");
    check(missing_language.language==5&&missing_language.row_labels[0]==std::string("Caf\xe9")&&
          missing_language.row_label_languages[0]==1,
          "missing localized interaction label retains English fallback code page and source language");
    data=iff(chunk("TTAB",128,ttab()));write(path,data);
    const auto no_labels=saved_object_interactions({path.string(),"",7,object});
    check(no_labels.state==SavedObjectInteractions::State::found&&!no_labels.row_labels[0],"missing TTAs leaves decoded interaction unlabeled");
    const std::vector<SavedTestFunctionResult> eligible_row{{SavedTestFunctionResult::State::eligible,0x1002,3,{}}};
    odh::World actor_world;check(actor_world.create(1,odh::EntityKind::avatar,{0,66,0,0}),"actor permission profile created");
    check(actor_world.set_person_data(1,32,0)&&actor_world.set_person_data(1,34,0)&&
          actor_world.set_person_data(1,58,27)&&actor_world.set_person_data(1,65,0),"resident adult permission profile initialized");
    check(actor_world.set_position(1,{16,16,1,0}),"menu caller is present in the lot");
    const auto* actor=actor_world.find(1);
    check(first_eligible_saved_interaction(no_labels,eligible_row,actor)==0,
        "zero-flag private action accepts resident with matching eligible TestFunction");
    auto auto_first_select=no_labels;auto_first_select.table->interactions[0].flags=0x100;
    check(first_eligible_saved_interaction(auto_first_select,eligible_row,actor)==0,
        "AutoFirstSelect remains available to manual input as the default menu row");
    auto semiglobal_action=no_labels;
    semiglobal_action.table->interactions[0].action=8213;
    odh::RoutineSet linked_actions{{odh::routine_key(8213),odh::Bhav{0x8002,{}, {},odh::Bhav::Metadata{0,4,4,0}}}};
    check(!first_eligible_saved_interaction(semiglobal_action,eligible_row,actor),
        "semiglobal ActionFunction is rejected when its exact linked code bank is absent");
    check(first_eligible_saved_interaction(semiglobal_action,eligible_row,actor,&linked_actions)==0,
        "semiglobal ActionFunction is accepted only when present in the explicit linked bank");
    auto global_action=no_labels;global_action.table->interactions[0].action=321;
    odh::RoutineSet global_actions{{odh::routine_key(321),{0x8002,{{2,254,255,{1,0,1,0,0,2,7,7}}},{},
        odh::Bhav::Metadata{0,4,4,0}}}};
    const odh::RoutineSet empty_globals;
    odh::RoutineSet wrong_globals{{odh::routine_key(322),global_actions.begin()->second}};
    check(!first_eligible_saved_interaction(global_action,eligible_row,actor)&&
        !first_eligible_saved_interaction(global_action,eligible_row,actor,nullptr,&empty_globals)&&
        !first_eligible_saved_interaction(global_action,eligible_row,actor,nullptr,&wrong_globals)&&
        !first_eligible_saved_interaction(global_action,eligible_row,actor,&global_actions),
        "global action needs its exact key in the explicit global bank rather than absent, empty, wrong-key or semiglobal input");
    check(first_eligible_saved_interaction(global_action,eligible_row,actor,nullptr,&global_actions)==0,
        "global action with an eligible private check uses the explicitly supplied global action bank");
    auto unavailable=eligible_row;unavailable[0].state=SavedTestFunctionResult::State::unsupported;
    check(!first_eligible_saved_interaction(global_action,unavailable,actor,nullptr,&global_actions),
        "global action cannot turn a missing or unsupported private check into permission");
    auto checked_global=global_action;checked_global.table->interactions[0].test=322;
    const std::vector<SavedTestFunctionResult> eligible_global_check{{SavedTestFunctionResult::State::eligible,322,1,{}}};
    check(!first_eligible_saved_interaction(checked_global,eligible_global_check,actor,nullptr,&global_actions),
        "global action cannot offer an absent nonzero global check even with a supplied eligibility record");
    global_actions.emplace(odh::routine_key(322),global_actions.begin()->second);
    check(first_eligible_saved_interaction(checked_global,eligible_global_check,actor,nullptr,&global_actions)==0,
        "global action and evaluated global check both require their exact supplied keys");
    auto failed_global_check=eligible_global_check;failed_global_check[0].state=SavedTestFunctionResult::State::ineligible;
    check(!first_eligible_saved_interaction(checked_global,failed_global_check,actor,nullptr,&global_actions),
        "known global action and check do not override a false check result");
    checked_global.table->interactions[0].test=8213;
    const std::vector<SavedTestFunctionResult> eligible_semi_check{{SavedTestFunctionResult::State::eligible,8213,1,{}}};
    check(!first_eligible_saved_interaction(checked_global,eligible_semi_check,actor,nullptr,&global_actions)&&
        first_eligible_saved_interaction(checked_global,eligible_semi_check,actor,&linked_actions,&global_actions)==0,
        "global action with semiglobal check still needs the exact linked check bank");
    auto unchecked_global=global_action;unchecked_global.table->interactions[0].test=0;
    const std::vector<SavedTestFunctionResult> no_check{{SavedTestFunctionResult::State::eligible,0,0,{}}};
    check(first_eligible_saved_interaction(unchecked_global,no_check,actor,nullptr,&global_actions)==0&&
        !first_eligible_saved_interaction(unchecked_global,eligible_row,actor,nullptr,&global_actions),
        "unchecked global action retains the explicit zero-check identity rather than accepting a mismatched test record");
    check(actor_world.set_person_data(1,32,1),"temporarily mark caller as visitor for global permission gate");
    check(!first_eligible_saved_interaction(global_action,eligible_row,actor,nullptr,&global_actions),
        "global action availability does not grant manual permission to a visitor");
    check(actor_world.set_person_data(1,32,0),"restore resident menu caller");
    check(global_action.source==no_labels.source&&global_action.asset==no_labels.asset&&
        global_action.table_source==no_labels.table_source&&global_action.table_origin==no_labels.table_origin&&
        global_action.row_labels==no_labels.row_labels&&global_action.row_label_languages==no_labels.row_label_languages,
        "global action classification does not replace table, private-code or localized-label provenance");
    auto multiple=no_labels;
    multiple.table->interactions.push_back(multiple.table->interactions[0]);
    multiple.table->interactions.push_back(multiple.table->interactions[0]);
    multiple.table->interactions[1].action=0x1003;multiple.table->interactions[1].test=0x1004;
    multiple.table->interactions[2].action=0x1005;multiple.table->interactions[2].test=0x1006;
    const std::vector<SavedTestFunctionResult> mixed_rows{
        {SavedTestFunctionResult::State::eligible,0x1002,3,{}},
        {SavedTestFunctionResult::State::ineligible,0x1004,1,{}},
        {SavedTestFunctionResult::State::eligible,0x1006,2,{}}};
    const auto eligible_rows=eligible_saved_interaction_rows(multiple,mixed_rows,actor);
    check(eligible_rows==std::vector<std::size_t>{0,2},
        "interaction chooser enumerates eligible rows in TTAB order and filters failed tests");
    check(cycle_eligible_saved_interaction_row(eligible_rows,0,1)==2&&
          cycle_eligible_saved_interaction_row(eligible_rows,2,1)==0&&
          cycle_eligible_saved_interaction_row(eligible_rows,0,-1)==2,
        "interaction chooser cycles forward and backward across eligible rows");
    check(cycle_eligible_saved_interaction_row(eligible_rows,1,1)==0&&
          cycle_eligible_saved_interaction_row(eligible_rows,2,0)==2&&
          !cycle_eligible_saved_interaction_row({},std::nullopt,1),
        "interaction chooser recovers a stale selection at the first live row and handles empty rows");
    auto many=multiple;
    many.table->interactions.assign(96,multiple.table->interactions[0]);
    for(std::size_t i=0;i<96;++i)many.table->interactions[i].label_index=1000+i;
    auto many_tests=saved_menu_tests(*many.table);
    std::vector<SavedTestFunctionResult> many_results(96,{SavedTestFunctionResult::State::eligible,0x1002,0,{}});
    many_results[40].state=SavedTestFunctionResult::State::unsupported;
    const auto many_eligible=eligible_saved_interaction_rows(many,many_results,actor);
    check(many_tests.size()==96&&many_eligible.size()==95&&many_eligible.back()==95&&
        std::find(many_eligible.begin(),many_eligible.end(),69)!=many_eligible.end()&&
        std::find(many_eligible.begin(),many_eligible.end(),40)==many_eligible.end(),
        "complete authored menu includes late rows in order without treating unknown as eligible");
    auto global_menu=no_labels;
    data=iff(chunk("TTAB",8,ttab())+chunk("TTAs",8,labels()));write(path,data);
    append_saved_global_interactions(global_menu,{path.string(),"",0,{}});
    check(global_menu.table->interactions.size()==2&&global_menu.row_global==std::vector<bool>({false,true})&&
        global_menu.table->interactions[0].label_index==global_menu.table->interactions[1].label_index&&
        global_menu.row_labels[1]==std::optional<std::string>{"Sit"},
        "global namespace appends first global table and strings without conflating matching TTAIndex");
    odh::Ttab bank_plan_table{};bank_plan_table.status=odh::TtabStatus::decoded;
    const auto add_bank_plan_row=[&](std::uint16_t action,std::uint16_t test){
        odh::Interaction row{};row.action=action;row.test=test;
        bank_plan_table.interactions.push_back(row);
    };
    add_bank_plan_row(0x1001,0x1002);add_bank_plan_row(0x1001,0x1003);
    add_bank_plan_row(0x1001,0x1002);add_bank_plan_row(0x1004,0x2001);
    add_bank_plan_row(0x1005,1);add_bank_plan_row(0x2002,0x1006);
    const auto private_plan=saved_menu_action_routine_plan(bank_plan_table,0x1001);
    check(private_plan&&private_plan->checks.size()==2&&
          private_plan->checks[0].scope==odh::RoutineScope::private_object&&private_plan->checks[0].id==0x1002&&
          private_plan->checks[1].scope==odh::RoutineScope::private_object&&private_plan->checks[1].id==0x1003&&
          private_plan->complete_private&&!private_plan->linked_semiglobal,
          "same-action private checks share a complete-private menu owner plan");
    const auto linked_plan=saved_menu_action_routine_plan(bank_plan_table,0x1004);
    check(linked_plan&&linked_plan->checks.size()==1&&
          linked_plan->checks[0].scope==odh::RoutineScope::semiglobal&&linked_plan->checks[0].id==0x2001&&
          !linked_plan->complete_private&&linked_plan->linked_semiglobal,
          "semiglobal check selects a private-and-linked menu owner plan");
    const auto global_plan=saved_menu_action_routine_plan(bank_plan_table,0x1005);
    check(global_plan&&global_plan->checks.size()==1&&
          global_plan->checks[0].scope==odh::RoutineScope::global&&global_plan->checks[0].id==1&&
          global_plan->complete_private&&!global_plan->linked_semiglobal,
          "shared check retains full private owner for calls back into the selected object");
    const auto semiglobal_action_plan=saved_menu_action_routine_plan(bank_plan_table,0x2002);
    check(semiglobal_action_plan&&semiglobal_action_plan->complete_private==false&&
          semiglobal_action_plan->linked_semiglobal,
          "semiglobal action keeps its linked bank even when its check is private");
    check(!saved_menu_action_routine_plan(bank_plan_table,0x0100),
          "menu owner plan rejects action roots outside private and linked-semiglobal banks");
    auto visitor_allowed=no_labels;visitor_allowed.table->interactions[0].flags=1;
    check(first_eligible_saved_interaction(visitor_allowed,eligible_row,actor)==0,
        "AllowVisitors flag accepts resident adult");
    auto age_restricted=no_labels;age_restricted.table->interactions[0].flags=0x31;
    check(first_eligible_saved_interaction(age_restricted,eligible_row,actor)==0,
        "adult may use an action flagged AllowVisitors, NoChild, and NoDemoChild");
    age_restricted.table->interactions[0].flags=0x10;
    check(actor_world.set_person_data(1,58,17),"child permission age initialized");
    check(!first_eligible_saved_interaction(age_restricted,eligible_row,actor),
        "NoChild excludes a saved child from a manual interaction");
    age_restricted.table->interactions[0].flags=0x40;
    check(first_eligible_saved_interaction(age_restricted,eligible_row,actor)==0,
        "NoAdult does not exclude a saved child");
    age_restricted.table->interactions[0].flags=0x20;
    check(first_eligible_saved_interaction(age_restricted,eligible_row,actor)==0,
        "NoDemoChild is ignored in the full-game TS1 permission profile");
    check(actor_world.set_person_data(1,58,27),"resident adult age restored");
    age_restricted.table->interactions[0].flags=0x40;
    check(!first_eligible_saved_interaction(age_restricted,eligible_row,actor),
        "NoAdult excludes a saved adult from a manual interaction");
    check(actor_world.set_person_data(1,32,1)&&actor_world.set_person_data(1,34,1),"active visitor profile initialized");
    check(!first_eligible_saved_interaction(no_labels,eligible_row,actor),
        "visitor with greet status below two is rejected when AllowVisitors is clear");
    check(first_eligible_saved_interaction(visitor_allowed,eligible_row,actor)==0,
        "visitor with greet status below two is accepted when AllowVisitors is set");
    check(actor_world.set_person_data(1,34,2),"greet threshold profile initialized");
    check(first_eligible_saved_interaction(no_labels,eligible_row,actor)==0,
        "PersonType one with greet status two or greater is not treated as visitor");
    auto immediate_action=no_labels;immediate_action.table->interactions[0].flags=4;
    check(first_eligible_saved_interaction(immediate_action,eligible_row,actor)==0,
        "RunImmediately remains selectable through the ordinary source menu");
    auto unknown_flags=no_labels;unknown_flags.table->interactions[0].flags=2;
    check(first_eligible_saved_interaction(unknown_flags,eligible_row,actor)==0,
        "Joinable is not a TS1 permission rejection");
    unknown_flags.table->interactions[0].flags=3;
    check(first_eligible_saved_interaction(unknown_flags,eligible_row,actor)==0,
        "AllowVisitors and Joinable preserve TS1 permission result");
    check(actor_world.set_person_data(1,34,0)&&actor_world.set_person_data(1,65,8),"pet profile initialized");
    check(!first_eligible_saved_interaction(visitor_allowed,eligible_row,actor),
        "pet cannot use a row without a matching pet flag");
    auto pet_action=visitor_allowed;pet_action.table->interactions[0].flags|=1U<<10;
    check(first_eligible_saved_interaction(pet_action,eligible_row,actor)==0,
        "dog can select a TS1 AllowDogs interaction");
    pet_action.table->interactions[0].flags=1U|(1U<<9);
    check(!first_eligible_saved_interaction(pet_action,eligible_row,actor),
        "dog cannot select a cats-only interaction");
    check(actor_world.set_person_data(1,65,16)&&first_eligible_saved_interaction(pet_action,eligible_row,actor)==0,
        "cat can select a TS1 AllowCats interaction");
    auto mismatched=eligible_row;mismatched[0].routine=0x1003;
    check(actor_world.set_person_data(1,65,0),"resident gender profile restored");
    check(!first_eligible_saved_interaction(no_labels,mismatched,actor),"manual action selector requires TestFunction identity match");
    data=iff(chunk("TTAB",128,ttab())+chunk("TTAB",128,ttab()));write(path,data);
    bool duplicate=false;try{(void)saved_object_interactions({path.string(),"",7,object});}catch(const odh::FormatError&){duplicate=true;}
    check(duplicate,"duplicate owner TTAB is rejected");
    object.tree_table_id=0;check(saved_object_interactions({path.string(),"",7,object}).state==SavedObjectInteractions::State::no_tree_table_id,"zero table id distinguished");
    object.tree_table_id=128;write(path,iff(""));check(saved_object_interactions({path.string(),"",7,object}).state==SavedObjectInteractions::State::missing_ttab,"absent TTAB distinguished");
    data=iff(chunk("BHAV",4098,test_bhav())+chunk("BHAV",4097,test_bhav())+chunk("BHAV",256,test_bhav()));write(path,data);object.guid=123;
    const auto private_code=load_saved_private_routines({path.string(),"",7,object});
    check(private_code.size()==2&&private_code.count(odh::routine_key(4097))&&private_code.count(odh::routine_key(4098)),
        "action owner loads exact private BHAVs and excludes shared global code");
    bool owner_budget=false;try{(void)load_saved_private_routines({path.string(),"",7,object},1);}
    catch(const odh::FormatError&){owner_budget=true;}
    check(owner_budget,"private owner resource budget rejects overflow");
    data=iff(chunk("BHAV",4098,test_bhav())+chunk("BHAV",4098,test_bhav(49)));write(path,data);
    const auto duplicate_private=load_saved_private_routines({path.string(),"",7,object});
    check(duplicate_private.size()==1&&duplicate_private.at(odh::routine_key(4098)).instructions[0].opcode==49,
        "numeric resource cache uses the last duplicate ID payload in physical order");
    std::string large;
    for(unsigned i=0;i<269;++i)large+=chunk("BHAV",4096+i,test_bhav());
    write(path,iff(large));
    check(load_saved_private_routines({path.string(),"",7,object}).size()==269,
        "common resource budget admits a whole bank beyond the former accidental256 cap");
    std::string routine_error,instruction_error;
    try{(void)load_saved_private_routines({path.string(),"",7,object},268);}catch(const odh::FormatError& error){routine_error=error.what();}
    try{(void)load_saved_private_routines({path.string(),"",7,object},4096,268);}catch(const odh::FormatError& error){instruction_error=error.what();}
    check(routine_error.find("routine budget")!=std::string::npos&&routine_error.find(path.string())!=std::string::npos&&
        instruction_error.find("decoded-instruction budget")!=std::string::npos,
        "bounded full-bank diagnostics distinguish routine count from decoded instruction capacity");
    const auto named_chunk=[&](unsigned id,const std::string& label,unsigned opcode){
        auto value=chunk("BHAV",id,test_bhav(opcode));value.replace(12,label.size(),label);return value;
    };
    write(path,iff(named_chunk(400,"first",2)+named_chunk(400,"last",49)+named_chunk(401,"first",15)+
        named_chunk(0,"zero",15)+named_chunk(65535,"high",15)));
    const auto names=load_saved_tree_name_catalog({path.string(),"",7,object});
    const auto explicit_private=load_saved_named_routines({path.string(),"",7,object},false);
    const auto explicit_linked=load_saved_named_routines({path.string(),"",7,object},true);
    check(names.private_bhav_ids.at("first")==400&&names.private_bhav_ids.at("last")==400&&
        names.private_bhav_ids.at("zero")==0&&names.private_bhav_ids.at("high")==65535,
        "named first-label dictionary includes all raw ushort IDs without scope inference");
    check(explicit_private.at(odh::named_routine_key(400,false)).instructions[0].opcode==49&&
        explicit_private.count(odh::named_routine_key(0,false))&&explicit_private.count(odh::named_routine_key(65535,false))&&
        !explicit_private.count(odh::routine_key(400))&&explicit_linked.count(odh::routine_key(65535)),
        "first label resolves last cached ID body while named entry and numeric namespaces remain distinct");
    data=iff(chunk("BHAV",4098,call_bhav(4097))+chunk("BHAV",4097,call_bhav(4099))+
             chunk("BHAV",4099,test_bhav())+chunk("BHAV",4100,test_bhav()));write(path,data);
    const auto closure=load_saved_action_routines({path.string(),"",7,object},4098);
    check(closure.size()==3&&closure.count(odh::routine_key(4098))&&closure.count(odh::routine_key(4097))&&
          closure.count(odh::routine_key(4099))&&!closure.count(odh::routine_key(4100)),
          "action owner loads only reachable private call closure");
    bool missing_dependency=false;data=iff(chunk("BHAV",4098,call_bhav(4097)));write(path,data);
    try{(void)load_saved_action_routines({path.string(),"",7,object},4098);}
    catch(const odh::FormatError&){missing_dependency=true;}
    check(missing_dependency,"missing private action dependency is rejected without fallback");
    bool closure_limit=false;data=iff(chunk("BHAV",4098,call_bhav(4097))+chunk("BHAV",4097,test_bhav()));write(path,data);
    try{(void)load_saved_action_routines({path.string(),"",7,object},4098,1);}
    catch(const odh::FormatError&){closure_limit=true;}
    check(closure_limit,"private action closure routine budget is enforced");
    data=iff(chunk("BHAV",4098,test_bhav()));write(path,data);
    odh::World world;check(world.create(1,odh::EntityKind::avatar,{0,66,0,0})&&
        world.create(2,odh::EntityKind::object,{0,0,1}),"probe world entities created");
    check(world.define_type(2,123,456),"target identity bound");
    check(world.set_position(1,{16,16,1,0}),"TestFunction caller is present in the lot");
    const auto unchecked=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{0});
    check(unchecked.size()==1&&unchecked[0].state==SavedTestFunctionResult::State::eligible&&
        unchecked[0].steps==0,"TTAB zero TestFunction is eligible without executing global BHAV zero");
    auto blocked=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(blocked.size()==1&&blocked[0].state==SavedTestFunctionResult::State::ineligible&&blocked[0].steps==1,"isolated read-only TestFunction false result");
    check(blocked[0].diagnostic.find("BHAV PC 0 opcode 2")!=std::string::npos,
        "false TestFunction records its final bounded instruction");
    check(world.attach(2,0,1),"occupant inserted into object slot");
    auto eligible=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(eligible[0].state==SavedTestFunctionResult::State::eligible,"read-only probe sees live slot occupant");
    check(world.detach(2,0),"live occupant removed before action-boundary eligibility recheck");
    auto stale_eligibility=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(stale_eligibility[0].state==SavedTestFunctionResult::State::ineligible,
        "fresh TestFunction result supersedes an earlier eligible click-time result");
    data=iff(chunk("BHAV",4098,test_bhav(49)));write(path,data);
    auto unsupported=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(unsupported[0].state==SavedTestFunctionResult::State::unsupported&&
        unsupported[0].diagnostic.find("BHAV 4098 PC 0 opcode 49")!=std::string::npos,
        "unsupported primitive includes the exact routine and instruction");
    data=iff(chunk("BHAV",4098,write_bhav()));write(path,data);
    auto write_attempt=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(write_attempt[0].state==SavedTestFunctionResult::State::unsupported&&write_attempt[0].diagnostic.find("write")!=std::string::npos,"TestFunction write rejected by read-only memory view");
    const auto advertisements_world=world.snapshot();
    data=iff(chunk("BHAV",4098,expression_bhav({
        {7,0,19,0,0,5,12,7},{7,0,19,0,0,2,12,7},
        {7,0,23,0,0,5,28,7},{7,0,23,0,0,2,28,7},
        {7,0,0,0,0,5,29,7},{7,0,0,0,0,2,29,7}}))+
        chunk("BHAV",4099,expression_bhav({
        {7,0,0,0,0,2,12,7},{7,0,0,0,0,2,28,7},{7,0,0,0,0,2,29,7}})));write(path,data);
    const auto advertisements=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098,4099,4098,4099});
    check(advertisements.size()==4&&std::all_of(advertisements.begin(),advertisements.end(),[](const auto& row){
        return row.state==SavedTestFunctionResult::State::eligible;
    }),"each menu row has an isolated nullable advertisement dictionary across all three scopes");
    check(world.snapshot().entities[0].motives==advertisements_world.entities[0].motives&&
        world.snapshot().entities[0].object_words==advertisements_world.entities[0].object_words,
        "menu advertisement writes never change real motives or ObjectData");
    data=iff(chunk("BHAV",4098,test_bhav()));write(path,data);
    auto exhausted=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},0);
    check(exhausted[0].state==SavedTestFunctionResult::State::unsupported&&exhausted[0].diagnostic.find("budget")!=std::string::npos,"zero instruction budget is unsupported");
    data=iff(chunk("BHAV",4098,object_type_test_bhav(456)));write(path,data);
    const auto type_probe=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(type_probe.size()==1&&type_probe[0].state==SavedTestFunctionResult::State::eligible,
        "read-only TestFunction recognizes an object's explicitly bound master type");
    constexpr std::uint32_t token_guid=0x12345678;
    data=iff(chunk("BHAV",4098,inventory_test_bhav(token_guid)));write(path,data);
    check(world.set_person_data(1,31,7),"inventory probe actor neighbor id initialized");
    const auto no_edition=[](std::int16_t index)->std::optional<std::int16_t>{
        return index==20?std::optional<std::int16_t>{0}:std::nullopt;};
    const auto all_editions=[](std::int16_t index)->std::optional<std::int16_t>{
        return index==20?std::optional<std::int16_t>{255}:std::nullopt;};
    const auto inventory_missing=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(inventory_missing[0].state==SavedTestFunctionResult::State::unsupported,
        "edition TestFunction remains unsupported when runtime globals are unavailable");
    const auto edition_disabled=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,nullptr,no_edition);
    check(edition_disabled[0].state==SavedTestFunctionResult::State::ineligible,
        "game edition without expansion bit rejects the magic interaction");
    odh::Ngbh empty_inventory{};empty_inventory.inventory_present=true;
    const auto inventory_empty=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,&empty_inventory,all_editions);
    check(inventory_empty[0].state==SavedTestFunctionResult::State::ineligible,
        "all expansion bits allow the test to reach FindToken and known-empty inventory returns false");
    odh::Ngbh matching_inventory{};matching_inventory.inventory_present=true;
    matching_inventory.inventory_by_id[7].push_back({7,token_guid,1});
    const auto inventory_match=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,&matching_inventory,all_editions);
    check(inventory_match[0].state==SavedTestFunctionResult::State::eligible,
        "read-only TestFunction sees a matching saved-neighborhood inventory token");
    {
        // Source check mode shares neighborhood inventory while isolating
        // external VM registers outside the scheduler's live dispatch.
        data=iff(chunk("BHAV",4098,inventory_effect_bhav(token_guid)));write(path,data);
        odh::NeighborInventory shared_inventory(empty_inventory);
        odh::Vm registers(odh::RoutineSet{},world.vm_memory());registers.temps()[0]=123;
        const auto effects=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098,4098},
            4096,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,nullptr,false,
            {},{},&registers,false,{},nullptr,&shared_inventory);
        check(effects.size()==2&&effects[0].state==SavedTestFunctionResult::State::eligible&&
            effects[1].state==SavedTestFunctionResult::State::eligible&&shared_inventory.items(7)&&
            shared_inventory.items(7)->size()==1&&shared_inventory.items(7)->front().count==2,
            "shared TestFunction AddToken mutates the supplied live authority once per actual check");
        check(registers.temps()[0]==123,"check inventory mutation does not defeat external register isolation");
        const auto live=odh::neighbor_inventory_primitive(shared_inventory,world);
        odh::VmFrame frame{odh::routine_key(4098),0,{1,2,2,0},{},{}};
        odh::Instruction query{51,254,255,{3,7,0,0,static_cast<std::uint8_t>(token_guid),
            static_cast<std::uint8_t>(token_guid>>8),static_cast<std::uint8_t>(token_guid>>16),static_cast<std::uint8_t>(token_guid>>24)}};
        const auto seen=live(frame,query,registers.temps(),{});
        check(seen.temp_write&&seen.temp_write->value==2,
            "ordinary live InventoryOperations sees the exact TestFunction mutation authority");
    }
    data=iff(chunk("BHAV",4098,call_global_bhav(321))+chunk("BHAV",321,constant_true_bhav()));write(path,data);
    odh::RoutineSet global_bank;bool global_routine_found=false;
    with_iff_file(path.string(),[&](const odh::View& view){odh::iff_chunks(view,[&](const odh::Chunk& c){
        if(c.type=="BHAV"&&c.id==321){global_bank.emplace(odh::routine_key(c.id),odh::read_bhav(c.payload));global_routine_found=true;}});});
    check(global_routine_found,"synthetic linked global TestFunction owner decoded");
    const auto missing_global=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(missing_global[0].state==SavedTestFunctionResult::State::unsupported,
        "TestFunction global calls remain unsupported without their explicit bank");
    const auto linked_global=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,nullptr,{},&global_bank);
    check(linked_global[0].state==SavedTestFunctionResult::State::eligible&&linked_global[0].steps==2,
        "TestFunction can call a routine from its explicitly linked global bank");
    const auto evaluate_global_checks=[&](const std::vector<std::uint16_t>& ids,const odh::RoutineSet* globals,
        const odh::RoutineSet* semi=nullptr,const odh::TuningResources* resources=nullptr,std::uint64_t budget=128){
        return evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,ids,
            4096,budget,{0},semi,{},nullptr,nullptr,{},globals,nullptr,nullptr,std::nullopt,resources);
    };
    const auto load_global_check_banks=[&](odh::RoutineSet& globals,odh::RoutineSet& semi){
        globals.clear();semi.clear();with_iff_file(path.string(),[&](const odh::View& view){
            odh::iff_chunks(view,[&](const odh::Chunk& c){if(c.type!="BHAV")return;
                const auto key=odh::routine_key(c.id);
                if(key.scope==odh::RoutineScope::global)globals.emplace(key,odh::read_bhav(c.payload));
                else if(key.scope==odh::RoutineScope::semiglobal)semi.emplace(key,odh::read_bhav(c.payload));
            });});
    };
    odh::RoutineSet direct_globals,direct_semi;
    auto false_check=constant_true_bhav();false_check[14]=static_cast<char>(255);false_check[15]=static_cast<char>(254);
    data=iff(chunk("BHAV",321,constant_true_bhav())+chunk("BHAV",322,false_check));write(path,data);
    load_global_check_banks(direct_globals,direct_semi);
    const auto checked_globals=evaluate_global_checks({321,322},&direct_globals);
    check(checked_globals[0].state==SavedTestFunctionResult::State::eligible&&checked_globals[0].steps==1&&
        checked_globals[1].state==SavedTestFunctionResult::State::ineligible&&checked_globals[1].steps==1,
        "direct global TestFunctions run their exact supplied true and false routines");
    check(evaluate_global_checks({321},nullptr)[0].state==SavedTestFunctionResult::State::unsupported&&
        evaluate_global_checks({321},&wrong_globals)[0].state==SavedTestFunctionResult::State::unsupported,
        "direct global check remains unsupported without its exact linked key rather than searching the private file");
    data=iff(chunk("BHAV",321,call_bhav(4098))+chunk("BHAV",4098,call_bhav(8192))+
        chunk("BHAV",8192,tuning_test_bhav(261,5)));write(path,data);
    load_global_check_banks(direct_globals,direct_semi);
    odh::TuningResources direct_tuning;
    direct_tuning.owners.emplace(2,odh::TuningOwner{{{4098,{0,{0,0,0,0,0,5},0}}},{},odh::TuningSemiState::loaded});
    const auto actor_person_before_global=world.find(1)->person_data;
    const auto actor_words_before_global=world.find(1)->object_words,target_words_before_global=world.find(2)->object_words;
    const auto actor_attributes_before_global=world.find(1)->attributes,target_attributes_before_global=world.find(2)->attributes;
    const auto revision_before_global=world.spatial_revision();
    const auto recursive_global=evaluate_global_checks({321},&direct_globals,&direct_semi,&direct_tuning);
    check(recursive_global[0].state==SavedTestFunctionResult::State::eligible&&recursive_global[0].steps==3,
        "global check calls exact private then semiglobal code while retaining selected object's private tuning owner");
    direct_tuning.owners.at(2).private_tables.at(4098).constants[5]=6;
    check(evaluate_global_checks({321},&direct_globals,&direct_semi,&direct_tuning)[0].state==SavedTestFunctionResult::State::ineligible,
        "global root retains private resource context instead of replacing it with the global bank");
    check(evaluate_global_checks({321},&direct_globals)[0].state==SavedTestFunctionResult::State::unsupported&&
        evaluate_global_checks({321},&direct_globals,&direct_semi,&direct_tuning,2)[0].state==SavedTestFunctionResult::State::unsupported,
        "missing linked semiglobal and exhausted global-check call budgets remain explicit unsupported states");
    data=iff(chunk("BHAV",321,expression_bhav({{50,0,1,0,0,5,3,7}}))+
        chunk("BHAV",322,expression_bhav({{50,0,0,0,0,2,3,7}})));
    write(path,data);load_global_check_banks(direct_globals,direct_semi);
    const auto global_hide=evaluate_global_checks({321,322,0},&direct_globals);
    check(global_hide[0].state==SavedTestFunctionResult::State::ineligible&&
        global_hide[1].state==SavedTestFunctionResult::State::eligible&&global_hide[2].state==SavedTestFunctionResult::State::eligible,
        "global check HideInteraction output resets independently for checked and zero-check rows");
    data=iff(chunk("BHAV",321,expression_bhav({{49,0,1,0,0,5,3,7}})));
    write(path,data);load_global_check_banks(direct_globals,direct_semi);
    check(evaluate_global_checks({321},&direct_globals)[0].state==SavedTestFunctionResult::State::unsupported&&
        world.find(1)->person_data==actor_person_before_global&&world.find(1)->object_words==actor_words_before_global&&
        world.find(2)->object_words==target_words_before_global&&world.find(1)->attributes==actor_attributes_before_global&&
        world.find(2)->attributes==target_attributes_before_global&&world.spatial_revision()==revision_before_global,
        "direct global checks preserve live World and do not extend isolated menu output permission to adjacent fields");
    data=iff(chunk("BHAV",4098,call_bhav(321))+chunk("BHAV",321,zone_test_global_bhav()));write(path,data);
    global_bank.clear();with_iff_file(path.string(),[&](const odh::View& view){odh::iff_chunks(view,[&](const odh::Chunk& c){
        if(c.type=="BHAV"&&c.id==321)global_bank.emplace(odh::routine_key(c.id),odh::read_bhav(c.payload));});});
    const auto selected_lot_global=[](std::int16_t index)->std::optional<std::int16_t>{return index==10?std::optional<std::int16_t>{7}:std::nullopt;};
    odh::LotContext selected_lot(7,std::vector<std::int16_t>(32,0),odh::LotZoning{{7,1}});
    const auto zoning_without_context=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,nullptr,selected_lot_global,&global_bank);
    check(zoning_without_context[0].state==SavedTestFunctionResult::State::unsupported&&
        zoning_without_context[0].diagnostic.find("BHAV 321 PC 1 opcode 1")!=std::string::npos,
        "lot zoning GenericCall remains unsupported without its selected LotContext");
    const auto zoning_with_context=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,nullptr,selected_lot_global,&global_bank,&selected_lot);
    check(zoning_with_context[0].state==SavedTestFunctionResult::State::eligible,
        "read-only lot zoning GenericCall uses the selected lot resource context");
    data=iff(chunk("BHAV",4098,budget_test_bhav(20,1)));write(path,data);
    const auto budget_available=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,500);
    const auto budget_unavailable=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,-1);
    check(budget_available[0].state==SavedTestFunctionResult::State::eligible&&
          budget_unavailable[0].state==SavedTestFunctionResult::State::ineligible,
        "read-only TS1 Budget TestFunction checks the active family's available balance");
    data=iff(chunk("BHAV",4098,budget_test_bhav(20,0)));write(path,data);
    const auto budget_write=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        4096,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,500);
    check(budget_write[0].state==SavedTestFunctionResult::State::unsupported&&
          budget_write[0].diagnostic.find("writer")!=std::string::npos,
        "read-only TS1 Budget rejects attempts to change the family's balance");
    check(world.create(3,odh::EntityKind::object)&&world.declare_object_word(3,59,12)&&
          world.create(4,odh::EntityKind::object)&&world.declare_object_word(4,59,13),
        "menu category enumeration candidates carry distinct live Category words");
    data=iff(chunk("BHAV",4098,category_test_bhav(3)));write(path,data);
    const auto category_menu=[&](std::int16_t category){
        return evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
            4096,128,{category});
    };
    const auto category_revision=world.spatial_revision();
    check(category_menu(12)[0].state==SavedTestFunctionResult::State::eligible,
        "TestFunction category mode6 uses Param0 and publishes the actual StackObject cursor");
    check(category_menu(13)[0].state==SavedTestFunctionResult::State::ineligible,
        "changing Param0 selects the other category instead of a fixed object");
    check(category_menu(99)[0].state==SavedTestFunctionResult::State::ineligible,
        "TestFunction category mode6 returns false for an empty category");
    check(world.spatial_revision()==category_revision&&world.find(3)->object_words.at(59)==12&&
          world.find(4)->object_words.at(59)==13,
        "category menu checks keep the live world unchanged");
    // Menu output is scratch per row. Setting it never mutates either live
    // entity, including the StackObject alias when it resolves to the caller.
    check(world.declare_object_word(1,50,9)&&world.declare_object_word(2,50,7),
        "live HideInteraction fields initialized distinctly from menu scratch");
    const auto revision_before_menu=world.spatial_revision();
    data=iff(chunk("BHAV",4098,expression_bhav({{50,0,1,0,0,5,3,7}}))+
             chunk("BHAV",4099,constant_true_bhav()));write(path,data);
    const auto hidden_then_clear=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098,4099});
    check(hidden_then_clear[0].state==SavedTestFunctionResult::State::ineligible&&
          hidden_then_clear[1].state==SavedTestFunctionResult::State::eligible,
        "HideInteraction=1 hides only its row and resets before the next check");
    const auto clear_before_no_check=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098,0});
    check(clear_before_no_check[0].state==SavedTestFunctionResult::State::ineligible&&
          clear_before_no_check[1].state==SavedTestFunctionResult::State::eligible,
        "zero TestFunction also resets a previous row's HideInteraction output");
    data=iff(chunk("BHAV",4098,expression_bhav({{50,0,2,0,0,5,3,7}})));write(path,data);
    const auto hidden_two=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(hidden_two[0].state==SavedTestFunctionResult::State::eligible,
        "HideInteraction suppresses exactly value one as in the reference");
    data=iff(chunk("BHAV",4098,expression_bhav({
        {50,0,1,0,0,5,3,7}, // MyObject.HideInteraction = 1.
        {0,0,1,0,0,5,10,7}, // StackObject = caller.
        {50,0,1,0,0,2,4,7}, // Alias sees the same output.
        {50,0,0,0,0,5,4,7}, // Alias clears it.
        {50,0,0,0,0,2,3,7}})));write(path,data);
    const auto hide_alias=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(hide_alias[0].state==SavedTestFunctionResult::State::eligible,
        "caller MyObject and StackObject aliases share isolated menu output");
    data=iff(chunk("BHAV",4098,expression_bhav({{50,0,1,0,0,5,4,7}})));write(path,data);
    const auto noncaller_hide=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(noncaller_hide[0].state==SavedTestFunctionResult::State::unsupported,
        "menu output does not permit writes to another entity's HideInteraction");
    data=iff(chunk("BHAV",4098,expression_bhav({{49,0,1,0,0,5,3,7}})));write(path,data);
    const auto adjacent_write=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(adjacent_write[0].state==SavedTestFunctionResult::State::unsupported,
        "allowing HideInteraction does not permit adjacent live object writes");
    check(world.find(1)->object_words.at(50)==9&&world.find(2)->object_words.at(50)==7&&
          world.spatial_revision()==revision_before_menu,
        "successful, hidden and rejected menu checks leave live World unchanged");
    data=iff(chunk("BHAV",4098,constant_true_bhav()));write(path,data);
    check(world.declare_object_word(1,34,1),"caller Hidden initialized");
    const auto hidden_actor=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098,0});
    check(hidden_actor[0].state==SavedTestFunctionResult::State::ineligible&&
          hidden_actor[1].state==SavedTestFunctionResult::State::ineligible,
        "Hidden caller suppresses checked and unchecked interactions");
    check(world.declare_object_word(1,34,2),"caller Hidden set to non-one value");
    const auto visible_two=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098});
    check(visible_two[0].state==SavedTestFunctionResult::State::eligible,
        "Hidden source condition tests exact value one");
    check(world.declare_object_word(1,34,0),"caller Hidden restored");
    odh::World out_of_world;
    check(out_of_world.create(1,odh::EntityKind::avatar,{0,66,0,0})&&
          out_of_world.create(2,odh::EntityKind::object,{0,0,1})&&out_of_world.define_type(2,123,456),
        "outside-lot caller fixture created without fabricated position");
    const auto absent_actor=evaluate_saved_test_functions({path.string(),"",7,object},out_of_world,1,2,{4098,0});
    check(absent_actor[0].state==SavedTestFunctionResult::State::ineligible&&
          absent_actor[1].state==SavedTestFunctionResult::State::ineligible,
        "outside-lot caller has no manual checked or unchecked menu entry");
    data=iff(chunk("BHAV",4098,tuning_test_bhav()));write(path,data);
    odh::TuningResources object_tuning;odh::TuningOwner tuning_owner;
    tuning_owner.private_tables[4098]={0,{0,0,0,0,0,0},0};
    object_tuning.owners.emplace(2,tuning_owner);
    const auto evaluate_tuning=[&](const odh::TuningResources& resources){
        return evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
            4096,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,&resources);
    };
    const auto tuning_zero=evaluate_tuning(object_tuning);
    check(tuning_zero[0].state==SavedTestFunctionResult::State::eligible,
        "read-only TestFunction reads private BCON4098 constant5 from the explicit object owner");
    object_tuning.owners.at(2).private_tables[4098].constants[5]=9;
    const auto tuning_nonzero=evaluate_tuning(object_tuning);
    check(tuning_nonzero[0].state==SavedTestFunctionResult::State::ineligible,
        "private tuning value changes the TestFunction comparison");
    object_tuning.owners.at(2).private_tables.clear();
    const auto tuning_missing_constant=evaluate_tuning(object_tuning);
    check(tuning_missing_constant[0].state==SavedTestFunctionResult::State::eligible,
        "loaded private tuning with a missing BCON key follows Simitone's zero default");
    object_tuning.owners.clear();
    const auto tuning_unlinked=evaluate_tuning(object_tuning);
    check(tuning_unlinked[0].state==SavedTestFunctionResult::State::unsupported,
        "private tuning remains unsupported when the exact object resource is not linked");
    const auto evaluate_context_tuning=[&](const odh::TuningResources& resources,bool global_loaded=false){
        return evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
            4096,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,&resources,global_loaded);
    };
    odh::TuningResources context_tuning;
    data=iff(chunk("BHAV",4098,tuning_test_bhav(17179,13)));write(path,data);
    check(evaluate_context_tuning(context_tuning)[0].state==SavedTestFunctionResult::State::unsupported,
        "unproven empty globals do not invent a zero constant");
    context_tuning.globals[262]={0,std::vector<std::uint16_t>(28,0),0};
    context_tuning.globals.at(262).constants[27]=13;
    check(evaluate_context_tuning(context_tuning)[0].state==SavedTestFunctionResult::State::eligible,
        "packed global BCON262 constant27 reads the exact supplied value without a private owner");
    context_tuning.globals.clear();
    data=iff(chunk("BHAV",4098,tuning_test_bhav(17179,0)));write(path,data);
    check(evaluate_context_tuning(context_tuning,true)[0].state==SavedTestFunctionResult::State::eligible,
        "explicitly loaded empty global bank follows the source zero default");
    context_tuning.globals[262]={0,{4},0};
    check(evaluate_context_tuning(context_tuning,true)[0].state==SavedTestFunctionResult::State::eligible,
        "missing constant in an explicitly loaded global table follows the source zero default");
    data=iff(chunk("BHAV",4098,tuning_test_bhav(8325,19)));write(path,data);
    context_tuning.owners.emplace(2,odh::TuningOwner{});
    context_tuning.owners.at(2).private_tables[8193]={0,{0,0,0,0,0,19},0};
    check(evaluate_context_tuning(context_tuning)[0].state==SavedTestFunctionResult::State::unsupported,
        "unknown semiglobal state cannot silently use private tuning");
    context_tuning.owners.at(2).semiglobal_state=odh::TuningSemiState::absent;
    check(evaluate_context_tuning(context_tuning)[0].state==SavedTestFunctionResult::State::eligible,
        "proven absent semiglobal tuning falls back to the source private table id");
    odh::set_semiglobal_tuning(context_tuning.owners.at(2),{{8193,{0,{0,0,0,0,0,23},0}}});
    check(evaluate_context_tuning(context_tuning)[0].state==SavedTestFunctionResult::State::ineligible,
        "loaded semiglobal value takes precedence over private fallback");
    data=iff(chunk("BHAV",4098,tuning_test_bhav(8325,23)));write(path,data);
    check(evaluate_context_tuning(context_tuning)[0].state==SavedTestFunctionResult::State::eligible,
        "packed semiglobal constant resolves its explicitly loaded bank");
    data=iff(chunk("BHAV",4098,tuning_test_bhav(8325,0)));write(path,data);
    context_tuning.owners.at(2).semiglobal_tables.clear();
    check(evaluate_context_tuning(context_tuning)[0].state==SavedTestFunctionResult::State::eligible,
        "loaded empty semiglobal tuning yields zero rather than private fallback");
    context_tuning.owners.clear();
    check(evaluate_context_tuning(context_tuning)[0].state==SavedTestFunctionResult::State::unsupported,
        "semiglobal lookup rejects an unbound object owner");
    // Real opcode50 in a synchronous check emits separate ordered choices;
    // repeated rows must not share variant state or replay per variant.
    data=iff(chunk("BHAV",4098,test_bhav(50)));write(path,data);
    unsigned menu_string_calls=0;
    const SavedMenuStringResolver menu_string=[&](const odh::VmFrame& frame,const odh::Instruction&)->SavedMenuStringResult{
        ++menu_string_calls;
        check(frame.context.stack_object==2,"dynamic menu retains actual StackObject");
        return {true,std::string("Social/Talk"),{}};
    };
    const auto dynamic=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098,4098},
        256,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,nullptr,false,menu_string);
    check(dynamic.size()==2&&dynamic[0].state==SavedTestFunctionResult::State::eligible&&
        dynamic[1].state==SavedTestFunctionResult::State::eligible&&menu_string_calls==2&&
        dynamic[0].variants.size()==1&&dynamic[1].variants.size()==1&&
        dynamic[0].variants[0].param0==2&&dynamic[0].variants[0].name==std::optional<std::string>{"Social/Talk"},
        "dynamic menu variants are produced once per actual row check and retain name and Param0");
    const auto denied_menu=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
        128,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,nullptr,false,
        menu_string,{},nullptr,false,{0x10});
    check(denied_menu[0].state==SavedTestFunctionResult::State::ineligible&&menu_string_calls==2,
        "source permission denial occurs before the check and its dynamic label callback");
    const auto limited_menu=evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098,4098},
        1,1,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,nullptr,false,menu_string);
    check(limited_menu[1].state==SavedTestFunctionResult::State::unsupported&&
        limited_menu[1].diagnostic.find("budget exhausted")!=std::string::npos,
        "aggregate menu budget exhaustion is explicit unsupported for unexecuted rows");
    {
        odh::NeighborRecord record{};record.version=4;record.neighbor_id=11;record.guid=10011;record.person_mode=1;
        record.person_data=std::vector<std::int16_t>(80);(*record.person_data)[58]=23;(*record.person_data)[61]=201;
        odh::NeighborContext neighbors(odh::Nbrs{0,62,{{1,record}},0});
        odh::RelationshipState relations(neighbors);odh::Family family{};family.house_number=7;family.family_friends=4;
        odh::FamilyContext families({{201,family}});
        // Earlier category tests own object IDs3/4. Every earlier avatar also
        // needs a known resource identity before a first-GUID-match query.
        check(world.define_type(1,10001)&&world.create(5,odh::EntityKind::avatar,{})&&world.define_type(5,10011),
            "neighbor query has complete avatar identities and an actual live matching avatar");
        unsigned queries=0;bool family_available=true;
        odh::VmMemory source_memory;
        source_memory.read=[&](const odh::VmFrame& frame,std::uint8_t scope,std::int16_t field)->std::optional<std::int16_t>{
            if(scope!=24)return {};
            ++queries;
            check(frame.context.entity_references_bound&&frame.context.stack_object_generation==0,
                "neighbor lookup retains raw numeric StackObject even when its entity-reference role is known nil");
            return odh::ts1_neighbor_info(neighbors,world,family_available?&families:nullptr,201,&relations,frame.context.stack_object,field);
        };
        const auto menu_neighbor=[&](std::int16_t neighbor,std::int16_t field,std::int16_t expected){
            const auto n=static_cast<std::uint16_t>(neighbor),f=static_cast<std::uint16_t>(field),v=static_cast<std::uint16_t>(expected);
            data=iff(chunk("BHAV",4098,expression_bhav({
                {0,0,static_cast<std::uint8_t>(n),static_cast<std::uint8_t>(n>>8),0,5,10,7},
                {static_cast<std::uint8_t>(f),static_cast<std::uint8_t>(f>>8),static_cast<std::uint8_t>(v),static_cast<std::uint8_t>(v>>8),0,2,24,7}})));
            write(path,data);
            return evaluate_saved_test_functions({path.string(),"",7,object},world,1,2,{4098},
                128,128,{0},nullptr,{},nullptr,nullptr,{},nullptr,nullptr,nullptr,std::nullopt,nullptr,false,
                {},{},nullptr,false,{},&source_memory).front();
        };
        const std::array<std::int16_t,10> expected{5,1,23,0,0,0,7,1,0,4};
        for(std::int16_t field=0;field<10;++field)
            check(menu_neighbor(11,field,expected[field]).state==SavedTestFunctionResult::State::eligible,
                "manual menu uses the complete real NeighborInStackObject field family");
        check(queries==10,"each source neighbor field is queried once without an extra replay");
        family_available=false;
        check(menu_neighbor(11,1,0).state==SavedTestFunctionResult::State::unsupported&&
            menu_neighbor(11,6,0).state==SavedTestFunctionResult::State::unsupported&&
            menu_neighbor(11,9,0).state==SavedTestFunctionResult::State::unsupported,
            "unavailable family provider never becomes a false/zero menu result");
        check(menu_neighbor(11,10,0).state==SavedTestFunctionResult::State::unsupported&&
            menu_neighbor(999,10,0).state==SavedTestFunctionResult::State::eligible,
            "existing neighbor invalid field remains unsupported while proven absent neighbor follows source zero-before-field-validation");
    }
    std::filesystem::remove_all(root);std::cout<<checks<<" saved interaction discovery checks passed (synthetic IFF only)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
