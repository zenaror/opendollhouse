#include "../tools/dialog_text_substitutions.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <iostream>
#include <stdexcept>
using namespace odh;using namespace odh_host;
namespace {unsigned checks=0;void require(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}}
int main(){try{
    World world;require(world.create(7,EntityKind::avatar,{2,88,1})&&world.define_type(7,77)&&world.create(9,EntityKind::object,{2,0,1})&&world.define_type(9,99),"actual independent caller and object authorities");
    VmFrame frame{routine_key(4096),0,{7,7,9},{17},{9,4,301,9,1,5}};require(world.bind_context_live(frame.context),"bound source frame epochs");
    require(world.write(frame,1,0,42),"actual StackObject attribute source");
    NeighborRecord n{};n.version=4;n.neighbor_id=9;n.guid=909;n.person_mode=1;n.person_data=std::vector<std::int16_t>(80);n.name="User00009.iff";
    NeighborContext neighbors({0,62,{{1,n}},0});Vm source({},world.vm_memory()),unrelated({},world.vm_memory());source.temps()[0]=23;source.temp_xl()[0]=123456;unrelated.temps()[0]=999;
    DialogTextContext context{world,frame,&source,&neighbors,DialogTextTable{"current","resource",301}};DialogTextProviders providers;
    std::vector<std::tuple<std::int16_t,unsigned,DialogEntityCatalogPolicy>> policies;
    providers.entity_runtime_name=[](const Entity&){return DialogTextValue{DialogTextAvailability::absent,{}};};
    providers.entity_catalog=[&](const Entity& e,unsigned i,DialogEntityCatalogPolicy policy){
        policies.emplace_back(e.id,i,policy);return DialogTextValue{DialogTextAvailability::value,i?"Catalog description":e.id==7?"Caller name":"Object name"};};
    providers.neighbor_catalog=[](auto guid,unsigned){require(guid==909,"Neighbor resolves raw StackObjectID through NBRS GUID, not World entity GUID");return DialogTextValue{DialogTextAvailability::value,"Eva"};};
    std::string why;const auto before=encode_world_snapshot(world.snapshot());
    const auto expanded=expand_ts1_dialog_text("$Me|$Object|$DynamicObjectName|$Neighbor|$NameLocal:0|$ObjectLocal:0|$CatalogLocal:0|$Param:0|$Local:1|$Temp:0|$TempXL:0|$Attribute:0",context,providers,why);
    require(expanded==std::optional<std::string>{"Caller name|Object name|Object name|Eva|Object name|Object name|Catalog description|17|4|23|123456|42"},"whole name/register/attribute family uses exact source authorities without NBRS filenames or primary VM fallback");
    using Policy=DialogEntityCatalogPolicy;
    require(policies==std::vector<std::tuple<std::int16_t,unsigned,Policy>>{{7,0,Policy::own},{9,0,Policy::prefer_master},{9,0,Policy::prefer_master},{9,0,Policy::own},{9,0,Policy::own},{9,1,Policy::own}},
        "each token explicitly selects own versus master CTSS policy without conflating caller and object display");
    auto runtime_named=providers;runtime_named.entity_runtime_name=[](const Entity& e){return e.id==7?DialogTextValue{DialogTextAvailability::value,"Actual selected BodySTR name"}:DialogTextValue{DialogTextAvailability::absent,{}};};
    require(expand_ts1_dialog_text("$Me",context,runtime_named,why)==std::optional<std::string>{"Actual selected BodySTR name"},"actual avatar/group Name owner precedes catalog without drawing new RNG");
    runtime_named.entity_runtime_name=[](const Entity&){return DialogTextValue{DialogTextAvailability::value,{}};};
    require(expand_ts1_dialog_text("$Me",context,runtime_named,why)==std::optional<std::string>{"Caller name"},"known empty group Name falls through to own ToString rather than replacing a real CTSS name");
    require(before==encode_world_snapshot(world.snapshot())&&source.temps()[0]==23&&unrelated.temps()[0]==999,"all substitutions are observational");
    auto absent_neighbor=frame;absent_neighbor.context.stack_object=42;DialogTextContext absent{world,absent_neighbor,&source,&neighbors,{}};
    require(expand_ts1_dialog_text("$Neighbor",absent,providers,why)==std::optional<std::string>{"Unknown"},"known absent neighbor uses explicit pinned display fallback");
    auto unavailable=context;unavailable.neighbors=nullptr;require(!expand_ts1_dialog_text("$Neighbor",unavailable,providers,why),"missing neighbor backend is not known absence");
    auto missing=providers;missing.neighbor_catalog=[](auto,unsigned){return DialogTextValue{};};require(!expand_ts1_dialog_text("$Neighbor",context,missing,why),"unknown CTSS lookup remains unsupported");
    missing=providers;missing.neighbor_catalog=[](auto,unsigned){return DialogTextValue{DialogTextAvailability::absent,{}};};require(expand_ts1_dialog_text("$Neighbor",context,missing,why)==std::optional<std::string>{"Unknown"},"known absent CTSS is distinct from unavailable decoding");
    require(!expand_ts1_dialog_text("$Local:20",context,providers,why)&&!expand_ts1_dialog_text("$Temp:99",context,providers,why),"unavailable source selectors do not invent zero values");
    auto no_vm=context;no_vm.source_vm=nullptr;require(!expand_ts1_dialog_text("$Temp:0",no_vm,providers,why),"source register owner is mandatory");
    require(!expand_ts1_dialog_text("$ListObject",context,providers,why)&&!expand_ts1_dialog_text("$MoneyXL:0",context,providers,why)&&!expand_ts1_dialog_text("$DateLocal:0:1:2",context,providers,why),"unbound native list and locale owners remain explicit gaps");
    providers.global_table=[](auto id)->std::optional<DialogTextTable>{return DialogTextTable{"global","resource",id};};
    providers.entity_tables=[](const auto&,auto id)->std::optional<std::vector<DialogTextTable>>{return std::vector<DialogTextTable>{{"semi","resource",id},{"private","resource",id},{"global","resource",id}};};
    providers.string=[](const DialogTextTable& table,auto i){
        if(table.archive=="semi")return DialogTextValue{DialogTextAvailability::absent,{}};
        return DialogTextValue{DialogTextAvailability::value,i==5?"$DynamicStringLocal:5":table.archive+" $Temp:0"};};
    require(expand_ts1_dialog_text("$DynamicStringLocal:4|$DynamicStringLocal:4:2|$DynamicStringLocal:4:2:3",context,providers,why)==std::optional<std::string>{"current 23|global 23|private 23"},"dynamic current/global/entity lookup follows real table order and expands nested text in the source VM");
    require(!expand_ts1_dialog_text("$DynamicStringLocal:5",context,providers,why)&&why.find("recursion")!=std::string::npos,"cyclic dynamic strings refuse without silently truncating expansion");
    auto branching=providers;unsigned loads=0;
    // Each stage branches four ways and eventually becomes empty: output and
    // depth bounds alone would permit 4^9 pointless expansions.
    branching.string=[&](const DialogTextTable&,auto i){++loads;std::string text;
        if(i<9)for(unsigned n=0;n<4;++n)text+="$DynamicStringLocal:"+std::to_string(i+1);
        return DialogTextValue{DialogTextAvailability::value,text};};
    auto branch_frame=frame;branch_frame.locals={0,1,2,3,4,5,6,7,8,9};
    DialogTextBudget shared_work;DialogTextContext branch_context{world,branch_frame,&source,&neighbors,context.current_table,&shared_work};
    require(!expand_ts1_dialog_text("$DynamicStringLocal:0",branch_context,branching,why)&&why.find("cumulative")!=std::string::npos&&loads<=256,
        "acyclic branching-to-empty expansion is bounded by cumulative work, not output size alone");
    require(!expand_ts1_dialog_text("another field",branch_context,branching,why),"all prepared fields share the already consumed work lease");

    providers.fixed=[](auto v)->std::optional<std::string>{return "fixed:"+std::to_string(v);};providers.money=[](auto v)->std::optional<std::string>{return "money:"+std::to_string(v);};
    require(expand_ts1_dialog_text("$FixedLocal:1|$MoneyXL:0",context,providers,why)==std::optional<std::string>{"fixed:4|money:123456"},"formatter receives actual source values, without assuming host locale");
    providers.career_time=[](auto token,const auto& indexes)->std::optional<std::string>{require(token=="Job:"&&indexes==std::vector<std::uint16_t>{1,2},"original career helper keeps exact source selector contract");return "Original career";};
    require(expand_ts1_dialog_text("$Job:1:2",context,providers,why)==std::optional<std::string>{"Original career"},"managed dialog migration does not overwrite original career behavior");
    require(world.remove(9)&&world.create(9,EntityKind::object)&&world.define_type(9,99),"replace object numeric ID after binding");
    require(!expand_ts1_dialog_text("$Object",context,providers,why),"stale bound StackObject cannot resolve a replacement body");
    std::cout<<checks<<" dialog substitution authority checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
