// SPDX-License-Identifier: MPL-2.0
// Synthetic commercial-format resources + real native VM/geometry/journal.
// This proves the adapter contract, not a catalog item from the installed game.
#include "object_trade_session_adapter.hpp"
#include "original_avatar_position.hpp"
#include "odh/collision.hpp"
#include "odh/dynamic_object_groups.hpp"
#include "community_lot_object_scene.hpp"
#include "community_lot_document.hpp"
#include "household_placement_adapter.hpp"
#include "session_refresh.hpp"
#include "session_room_lighting.hpp"
#include "linux_family_overlay_store.hpp"
#include "linux_session_archive_store.hpp"
#include "odh/sha256.hpp"
#include "odh/generic_ts1_compatibility.hpp"
#include "odh/relationship_snapshot_codec.hpp"
#include "odh/seated_session_continuation.hpp"
#include "odh/room_lighting_codec.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/object_position_registry.hpp"
#include "odh/object_value_registry.hpp"
#include <chrono>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace odh;using namespace odh_host;
void require(bool yes,const std::string& why){if(!yes)throw std::runtime_error(why);}
void put(std::string& b,std::uint32_t n,unsigned width){for(unsigned i=0;i<width;++i)b.push_back(static_cast<char>((n>>(8*i))&255));}
void at(std::string& b,std::size_t offset,std::uint32_t n,unsigned width,bool big=false){for(unsigned i=0;i<width;++i)b.at(offset+i)=static_cast<char>((n>>(8*(big?width-1-i:i)))&255);}
std::string chunk(const char* type,std::uint16_t id,const std::string& payload){std::string b(76,'\0');b.replace(0,4,type);at(b,4,static_cast<std::uint32_t>(76+payload.size()),4,true);at(b,8,id,2,true);return b+payload;}
std::string iff(const std::vector<std::string>& chunks){std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');for(const auto& c:chunks)b+=c;return b;}
std::string assignments(const std::vector<std::pair<std::uint16_t,std::uint16_t>>& values){
    std::string b;put(b,0x8002,2);put(b,static_cast<std::uint32_t>(values.size()),2);b.append(8,'\0');
    for(std::size_t i=0;i<values.size();++i){put(b,2,2);b.push_back(static_cast<char>(i+1==values.size()?254:i+1));b.push_back(static_cast<char>(255));
        put(b,values[i].first,2);put(b,values[i].second,2);b.push_back(0);b.push_back(5);b.push_back(3);b.push_back(7);}
    return b;
}
std::string init_with_refresh(){auto b=assignments({{4,1},{13,128},{42,1},{23,2},{43,10}});at(b,2,10,2);b[12+4*12+2]=5;
    const auto expression=[&](unsigned lhs,unsigned rhs,unsigned left_scope,unsigned right_scope,unsigned next){
        put(b,2,2);b.push_back(static_cast<char>(next));b.push_back(static_cast<char>(255));
        put(b,lhs,2);put(b,rhs,2);b.push_back(0);b.push_back(5);
        b.push_back(static_cast<char>(left_scope));b.push_back(static_cast<char>(right_scope));};
    expression(0,65535,8,7,6); // Actual Init Thread selects unsigned career65535.
    expression(1,2,8,7,7);
    expression(0,12,0,33,8); // Keep the source JobData start hour in owned attribute0.
    for(const auto mode:{0U,2U}){put(b,7,2);b.push_back(static_cast<char>(mode==0?9:254));b.push_back(static_cast<char>(255));put(b,0,2);put(b,mode,2);put(b,0,4);}return b;}
std::string sleep_bhav(){std::string b;put(b,0x8002,2);put(b,1,2);b.push_back(0);b.push_back(4);b.append(6,'\0');
    put(b,0,2);b.push_back(static_cast<char>(254));b.push_back(static_cast<char>(255));b.append(8,'\0');return b;}
std::string far(const std::vector<std::pair<std::string,std::string>>& entries){std::string b="FAR!byAZ";put(b,1,4);put(b,0,4);std::vector<std::uint32_t> offsets;
    for(const auto& e:entries){offsets.push_back(static_cast<std::uint32_t>(b.size()));b+=e.second;}at(b,12,static_cast<std::uint32_t>(b.size()),4);put(b,static_cast<std::uint32_t>(entries.size()),4);
    for(std::size_t i=0;i<entries.size();++i){put(b,static_cast<std::uint32_t>(entries[i].second.size()),4);put(b,static_cast<std::uint32_t>(entries[i].second.size()),4);put(b,offsets[i],4);put(b,static_cast<std::uint32_t>(entries[i].first.size()),4);b+=entries[i].first;}return b;}
void write(const std::filesystem::path& p,const std::string& b){std::ofstream f(p,std::ios::binary);f.write(b.data(),static_cast<std::streamsize>(b.size()));require(bool(f),"write isolated format fixture");}
struct TemporaryDirectory {
    std::filesystem::path path;std::optional<std::string> old_config;
    TemporaryDirectory(){if(const auto* old=std::getenv("XDG_CONFIG_HOME"))old_config=old;
        path=std::filesystem::temp_directory_path()/("odh-object-command-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(path),"create isolated test profile");require(::setenv("XDG_CONFIG_HOME",path.c_str(),1)==0,"set isolated profile");}
    ~TemporaryDirectory(){if(old_config)::setenv("XDG_CONFIG_HOME",old_config->c_str(),1);else ::unsetenv("XDG_CONFIG_HOME");std::error_code ec;std::filesystem::remove_all(path,ec);}
};
SessionBundle source(){
    World w;require(w.create_saved_entity(1,EntityKind::avatar,123456,{},std::vector<std::int16_t>(80),std::vector<std::int16_t>(16),{},{},0),"real resident World banks");
    require(w.set_person_data(1,31,41)&&w.set_person_data(1,61,21)&&w.set_lockout_count_clock(0),"source family/person/clock facts");
    Vm vm(RoutineSet{});EntityThreadControllers controllers(w,1);require(controllers.bind(vm),"resident controllers");
    EntityVmScheduler scheduler(8);require(scheduler.add(1,vm,[](auto){return true;},[]{return true;},[]{return true;}),"source scheduler");
    SessionBundle b;b.generic_execution=true;b.resources.language_code=1;b.resources.expansion_mask=127;b.resources.resource_fingerprint.fill(0x5a);
    b.host.neighborhood_mode=1;b.host.house_number=7;b.host.family_chunk_id=21;b.host.family_number=12;b.host.resident_guid=123456;b.host.resident_neighbor_id=41;b.host.runtime_avatar_id=1;b.host.random_seed=1234;b.host.family_budget=8000;
    SessionHouseholdMember m;m.id=1;m.generation=w.find(1)->generation;m.guid=123456;m.neighbor_id=41;m.object_definition_id=128;m.character_asset="PlayerData/Characters/User1.iff";m.character_fingerprint.fill(0x31);b.household_roster.push_back(m);
    b.world=w.snapshot();b.relationships=RelationshipState{}.snapshot();b.runtime_entities={{1,vm.snapshot(),controllers.snapshot()}};b.scheduler=scheduler.snapshot();b.object_history=SessionObjectHistory{true,{},{}};return b;
}
void source_oow_publication(SessionObjectPositionContext context){
    for(bool known:{false,true})for(std::int16_t level:{1,2})for(bool attached:{false,true}){
        World local;
        for(std::int16_t id=1;id<=4;++id){
            require(known?local.create_runtime_object(id,0x1235,{0,0,1},{}):
                (local.create(id,EntityKind::object,{0,0,1})&&local.define_type(id,0x1235)),"source OOW whole physical tree");
            if(!known)require(local.declare_object_word(id,1,0)&&local.declare_object_word(id,23,0),"legacy source direction words");
            require(local.set_position(id,{24,24,level,0}),"source OOW initial level");
        }
        require(local.attach(1,0,2)&&local.attach(2,0,3)&&(!attached||local.attach(4,0,1)),"source OOW recursive physical attachments");
        bool authority=true;auto ctx=context;ctx.world=&local;ctx.memory=local.vm_memory();ctx.group={};ctx.position_phase={};
        ctx.source_still_matches=[&]{return authority;};
        auto services=make_session_object_position_services(ctx);const ObjectPositionIdentity root{1,local.find(1)->generation};
        require(services.change.publish_position(root,{},2)&&!local.find(1)->position&&!local.find(1)->attachment&&
            local.native_position({root.id,root.generation})==std::optional<EntityPosition>{{-16,-16,1,2}},
            "actual adapter source OOW publishes level1 from either storey or a containing slot");
        require(local.find(1)->native_slot0.has_value()==known&&
            (known?local.find(1)->object_words.at(14)==-1&&local.find(1)->object_words.at(26)==0:
                !local.find(1)->object_words.count(14)&&!local.find(1)->object_words.count(26)),
            "source scalar proof never backfills unknown hierarchy or raw parent fields");
        // PositionChange propagates the parent's complete source pose in its
        // real child order; publication must not flatten or detach the tree.
        require(local.find(2)->position&&local.refresh_contained_position(2)&&local.refresh_contained_position(3),"actual Post child pose propagation");
        for(std::int16_t id:{2,3})require(local.find(id)->attachment&&!local.find(id)->position&&
            local.native_position({id,local.find(id)->generation})==std::optional<EntityPosition>{{-16,-16,1,0}}&&
            local.find(id)->native_slot0.has_value()==known,"nested source OOW keeps attachment and canonical level across layouts");
        const auto bytes=encode_world_snapshot(local.snapshot());World reopened;
        require(reopened.restore_snapshot(decode_world_snapshot(bytes))&&encode_world_snapshot(reopened.snapshot())==bytes,
            "source OOW scalars and hierarchy knowledge survive actual World codec roundtrip");
        require(local.set_native_out_of_world_level({root.id,root.generation},2)&&services.change.publish_position(root,{},2)&&
            local.find(1)->native_out_of_world_level==std::optional<std::int16_t>{1},"repeated source sentinel writes1 rather than retaining an unrelated old level");
        const auto revision=local.spatial_revision();authority=false;
        require(!services.change.publish_position(root,{},2)&&local.spatial_revision()==revision,"expired source cannot publish position or scalar");
    }
    World legacy;require(legacy.create(1,EntityKind::object)&&legacy.declare_object_word(1,1,0)&&legacy.set_position(1,{24,24,2,0}),"generic geometry source");
    require(legacy.place_individual_on_floor(1,{},0)&&!legacy.find(1)->native_out_of_world_level&&!legacy.find(1)->native_slot0,
        "generic World null still does not infer native source level or hierarchy");
    const auto ref=WorldEntityReference{1,legacy.find(1)->generation};
    require(!publish_source_individual_position(legacy,{1,ref.generation+1},{},0,[]{return true;})&&
        !legacy.find(1)->native_out_of_world_level,"stale identity cannot supply new source scalar authority");
}
void physical_member_lifecycle(const std::filesystem::path& root){
    const auto game=root/"physical-game";std::filesystem::create_directories(game);
    const auto global=game/"Global.far";
    write(global,far({{"global.iff",iff({chunk("BHAV",256,assignments({{0,1}}))})}}));
    std::vector<DefinitionCandidate> definitions;
    for(std::uint32_t part=0;part<2;++part){
        std::string objd(190,'\0');at(objd,0,138,4);at(objd,18,4,2);at(objd,20,7,2);at(objd,22,part,2);at(objd,28,0x1235+part,4);
        std::string objf(16+33*4,'\0');at(objf,4,1,4);objf.replace(8,4,"fJBO");at(objf,12,33,4);
        at(objf,16+8*4+2,4100,2);at(objf,16+9*4+2,4101,2);
        const auto path=game/("part"+std::to_string(part)+".iff");
        write(path,iff({chunk("OBJD",128,objd),chunk("OBJf",128,objf),
            chunk("BHAV",4100,assignments({{0,88}})),chunk("BHAV",4101,assignments({{6,99}}))}));
        std::istringstream input(objd,std::ios::binary);
        definitions.push_back({path.string(),"",128,read_object_definition(View(input,objd.size()))});
    }
    DefinitionIndex index(std::move(definitions));ObjectTradeResourceLibrary resources(game,index,global,"global.iff");
    // The same real resource library is read from an actual VM primitive.
    // Pre-admission owns metadata only: it must not allocate any World body.
    const auto* admitted=&resources.creation_resource(0x1235,nullptr);
    World boundary_world;require(boundary_world.create(7,EntityKind::object),"creation cache boundary actor");
    const auto initial_world=boundary_world.snapshot();bool inspected=false;
    Vm boundary_vm({{routine_key(4096),Bhav{0x8002,{{60,254,255,{}}},{},Bhav::Metadata{0,0,0,0}}}},boundary_world.vm_memory(),{},
        [&](const VmFrame&,const Instruction&,const Ts1Temps&,const VmDispatch& dispatch){
            require(dispatch.origin&&dispatch.origin->dispatch_active(),"resource admission fixture is inside an actual VM dispatch");
            require(&resources.creation_resource(0x1235,dispatch.origin)==admitted&&resources.find(0x1235)==admitted,
                "cached immutable creation metadata is reused during dispatch without a second admission");
            bool rejected=false;try{(void)resources.creation_resource(0x1236,dispatch.origin);}
            catch(const FormatError& e){rejected=std::string(e.what())=="creation resource admission requires a dispatch-free boundary";}
            require(rejected&&!resources.find(0x1236),"new resource admission remains forbidden and absent during dispatch");
            inspected=true;return PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};
        });
    boundary_vm.start(routine_key(4096),{7,7,7});
    require(boundary_vm.run(1)==VmStatus::returned_true&&inspected,"creation cache test completed its genuine primitive");
    require(boundary_world.snapshot().entities.size()==initial_world.entities.size(),"pre-admission and cache lookup create no World body");
    const auto* newly_admitted=&resources.creation_resource(0x1236,&boundary_vm);
    require(newly_admitted==resources.find(0x1236),
        "same missing GUID can be admitted after the actual VM dispatch has ended");

    PreparedArchitecture prepared;prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
    SavedArchitecture raw;raw.width=raw.height=4;raw.altitude=std::vector<std::array<std::uint8_t,4>>(16);
    for(unsigned level=0;level<2;++level){prepared.levels[level].floors.assign(16,1);prepared.levels[level].walls.resize(16);
        raw.levels[level].floors=prepared.levels[level].floors;raw.levels[level].walls.resize(16);}
    const auto rooms=generate_prepared_room_map(prepared);World world;
    require(world.set_room_resolver([&](const auto& p){return geometric_room_at_runtime(rooms,p.x,p.y,p.level);}),"physical member actual room resolver");
    std::map<std::int16_t,std::unique_ptr<Vm>> threads;ObjectGroup group{0x9000,true,1,{}};
    for(std::int16_t id=1;id<=2;++id){
        const auto guid=static_cast<std::uint32_t>(0x1234+id);const auto& resource=resources.load(guid);
        require(world.create(id,EntityKind::object,{0,0,1})&&world.define_type(id,guid,0x9000),"physical member typed master");
        std::vector<EntryPoint> ep(27);ep[8].action=4100;ep[9].action=4101;
        require(world.define_entry_points(id,ep),"physical member resource entry points");
        for(std::int16_t n=0;n<=78;++n)if(n!=2&&n!=3&&n!=11&&n!=67)
            require(world.declare_object_word(id,n,0),"physical member constructor fields");
        require(world.set_position(id,{8+id*16,24,1,0}),"physical member floor position");
        group.members.push_back({id,world.find(id)->generation,static_cast<std::int16_t>((id-1)*16),0,0});
        threads[id]=std::make_unique<Vm>(resources.global_program(),ShareVmProgramTag{},world.vm_memory(),VmLimits{},VmPrimitive{},
            SharedRoutineOwners{{guid,std::make_shared<const VmProgram>(resource.code.runtime.private_routines)}},ShareVmOwnerProgramsTag{});
    }
    bool authority=true,profile=true,known=true;SessionObjectPositionContext ctx;
    ctx.world=&world;ctx.resources=&resources;ctx.prepared=&prepared;ctx.architecture=&raw;ctx.rooms=&rooms;
    ctx.source_still_matches=[&]{return authority;};ctx.memory=world.vm_memory();
    ctx.thread_for=[&](auto id){const auto it=threads.find(id);return it==threads.end()?nullptr:it->second.get();};
    ctx.owner_for=[&](auto id)->std::optional<CodeOwner>{const auto* e=world.find(id);return e&&e->definition?std::optional<CodeOwner>{e->definition->guid}:std::nullopt;};
    ctx.primitive_factory=[](const auto&){return VmPrimitive{};};
    ctx.group=[&](auto)->std::optional<ObjectGroup>{return known?std::optional<ObjectGroup>{group}:std::nullopt;};
    ctx.admit_group_member=[&](auto)->std::optional<bool>{return profile;};
    source_oow_publication(ctx);
    auto services=make_session_object_position_services(ctx);
    const ObjectPositionIdentity first{1,world.find(1)->generation},second{2,world.find(2)->generation};
    require(services.change.group&&services.change.group(first)->members.size()==2&&
        services.change.admit_single_ordinary(first)==std::optional<bool>{false},"group binding does not weaken singleton movement/trade admission");
    require(services.placement.same_group(first,second)==std::optional<bool>{true},"intersection grouping uses exact live physical authority");
    for(const auto id:{first,second}){
        const auto other=id.id==1?2:1;const auto old=world.find(other)->position;
        require(services.before_slot_transfer(id,true),"physical member deletion Pre: "+services.lifecycle_diagnostic());
        require(services.after_slot_transfer(id,true),"physical member Post: "+services.lifecycle_diagnostic());
        require(world.find(id.id)->object_words.at(0)==88&&world.find(id.id)->object_words.at(6)==99&&
            world.find(other)->position==old,"actual resource EP8/EP9 execute without moving another physical piece");
    }
    // Bind the same actual World/value/group owner into the shared session
    // placement consumer; this is not a copied or stand-in rotation service.
    SessionObjectTradeLedger ledger;ledger.version=2;ledger.revision=1;
    SessionObjectValueGroup price;price.group_id=price.revision=1;price.multitile=true;price.initial_price=100;price.base=ObjectTradeIdentity{first.id,first.generation};
    price.price_origin={SessionObjectPriceSource::constructed_catalog,0x1235,"fixture","physical",128,{},{}};
    ObjectGroupSnapshot owned;owned.groups={group};
    for(auto& member:owned.groups.front().members){const auto& r=resources.load(world.find(member.id)->definition->guid);
        member.origin=ObjectGroupMemberOrigin{ObjectGroupMemberSource::original_import,r.code.origin.guid,0x9000,r.code.origin.source,r.code.origin.asset,r.code.origin.definition_chunk_id};
        SessionObjectValueMember value;value.object=r.code.origin;value.object.id=member.id;value.object.generation=member.generation;value.object.master_guid=0x9000;
        value.offset_x=member.offset_x;value.offset_y=member.offset_y;value.lead_object=std::uint16_t(member.id==1?1:0);price.members.push_back(value);
        for(const auto field:std::vector<std::pair<std::int16_t,std::int16_t>>{{4,1},{13,128},{23,2},{27,1},{28,1},{42,1}})
            require(world.declare_object_word(member.id,field.first,field.second),"native adapter placement fields");
    }
    ledger.groups={price};ObjectValueRegistry values(world,ledger);require(values.bind(),"actual native adapter shared value owner");
    DynamicObjectGroupServices dynamic_services;dynamic_services.authority=ctx.source_still_matches;
    dynamic_services.publish_native_direction=[&](auto id,auto heading){return world.publish_native_object_heading(id,heading);};
    dynamic_services.execute_entry8=[&](auto id,auto flags){return services.execute_entry(id,8,0,{flags,0,0,0}).has_value();};
    DynamicObjectGroups dynamic(world,values,owned,std::move(dynamic_services));
    require(dynamic.bind_original_native_group(first,{first.id,first.generation},{{{first.id,first.generation},{0,0,0},{0,0,0}},
        {{second.id,second.generation},{1,0,0},{1,0,0}}}),"authenticate original native fields on same live owner");
    auto native_ctx=ctx;native_ctx.group=[&](auto)->std::optional<ObjectGroup>{return owned.groups.front();};
    native_ctx.authenticate_live_group=[&](const auto& g){return g.native_head==owned.groups.front().native_head&&g.members.size()==owned.groups.front().members.size();};
    native_ctx.begin_native_rotation=[&](auto id,auto heading){return dynamic.begin_native_rotation(id,heading);};
    native_ctx.rollback_native_rotation=[&](const auto& rotation){return dynamic.rollback_native_rotation(rotation);};
    native_ctx.native_current_offset=[&](auto id){return dynamic.native_offset(id);};
    native_ctx.native_group_head=[&](auto id){return dynamic.group_head(id);};native_ctx.native_group_next=[&](auto id){return dynamic.group_next(id);};
    native_ctx.dynamic_group_update=[&](auto id){return dynamic.update(id,false);};
    auto native=make_session_object_position_services(std::move(native_ctx));
    ObjectPositionValid native_valid(world,native.placement);ObjectPositionChange native_move(world,native_valid,native.change);
    const auto moved=native_move.apply_existing_group({first,{32,32,1,3},3,1,ctx.source_still_matches});
    require(moved.status==ObjectPositionChangeStatus::success,"actual session native rotation: "+moved.diagnostic);
    require(world.find(1)->object_words.at(1)==3&&world.find(2)->object_words.at(1)==3&&dynamic.native_offset({second.id,second.generation})->x==0&&
        world.find(1)->object_words.at(6)==99&&world.find(2)->object_words.at(6)==99,
        "session consumer preserves native odd heading/current fields and executes actual resource Post entries");
    require(!services.before_slot_transfer({first.id,first.generation+1},true),"stale physical generation rejected");
    known=false;require(!services.before_slot_transfer(first,true),"unknown physical group rejected");known=true;
    group.members[1].generation++;require(!services.before_slot_transfer(first,true),"incomplete group generation proof rejected");group.members[1].generation--;
    profile=false;require(!services.before_slot_transfer(first,true),"unsupported physical lifecycle profile rejected");profile=true;
    require(world.declare_object_word(1,40,static_cast<std::int16_t>(0x4000)),"set unsupported architectural profile");
    require(!services.before_slot_transfer(first,true),"group proof cannot bypass architectural profile");
    authority=false;require(!services.after_slot_transfer(second,true),"lost physical source authority rejected");
}
void community_scene_reopen(const std::filesystem::path& root){
    const auto game=root/"community-game";std::filesystem::create_directories(game);
    std::string objd(190,'\0');at(objd,0,138,4);at(objd,8,1,2);at(objd,18,4,2);
    at(objd,28,0x1234,4);at(objd,36,100,2);at(objd,44,1,2);at(objd,80,1,2);at(objd,116,1,2);at(objd,134,16,2);
    std::string objf(16+33*4,'\0');at(objf,4,1,4);objf.replace(8,4,"fJBO");at(objf,12,33,4);
    for(const auto& p:std::vector<std::pair<unsigned,unsigned>>{{0,4096},{1,4097},{8,4100},{11,4098},{12,4099}})at(objf,16+p.first*4+2,p.second,2);
    const auto object_file=game/"chair.iff",global_file=game/"Global.far";
    write(object_file,iff({chunk("OBJD",128,objd),chunk("OBJf",128,objf),
        chunk("BHAV",4096,assignments({{4,1},{13,128},{42,1},{23,2},{43,10}})),
        chunk("BHAV",4097,assignments({{0,77}})),chunk("BHAV",4100,assignments({{0,88}})),
        chunk("BHAV",4098,assignments({{6,11}})),chunk("BHAV",4099,assignments({{6,12}}))}));
    write(global_file,far({{"global.iff",iff({chunk("BHAV",256,assignments({{0,1}}))})}}));
    std::istringstream input(objd,std::ios::binary);
    DefinitionIndex index({{object_file.string(),"",128,read_object_definition(View(input,objd.size()))}});
    ObjectTradeResourceLibrary resources(game,index,global_file,"global.iff");
    SavedWorld original;original.world=std::make_unique<World>();original.relationships=std::make_unique<RelationshipState>();
    SessionResourceIdentity identity;identity.resource_fingerprint.fill(0x45);
    auto globals=RuntimeGlobals::from_legacy({}).snapshot();globals.words[32]=1;
    auto state=make_community_object_baseline(original,identity,globals,std::nullopt,0x123456789abcdef0ULL);
    require(state.history.complete&&state.history.reserved_entity_ids.empty()&&state.object_threads.empty(),
        "actorless baseline proves fresh history without reserving live IDs or inventing original Threads");
    PreparedArchitecture prepared;prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
    SavedArchitecture raw;raw.width=raw.height=4;raw.altitude=std::vector<std::array<std::uint8_t,4>>(16);
    for(unsigned level=0;level<2;++level){prepared.levels[level].floors.assign(16,1);prepared.levels[level].walls.resize(16);
        raw.levels[level].floors=prepared.levels[level].floors;raw.levels[level].walls.resize(16);}
    const auto rooms=generate_prepared_room_map(prepared);
    CommunityObjectCommandServices services;services.source_still_matches=[]{return true;};
    ObjectTradeSessionCommand command;command.kind=ObjectTradeSessionCommandKind::purchase;
    command.guid=0x1234;command.position={24,24,1,0};
    auto bought=prepare_community_object_command(resources,state,prepared,raw,rooms,command,services);
    require(bought.status==ObjectTradeTransactionStatus::prepared_success&&bought.candidate.has_value(),
        "actorless actual resource purchase: "+bought.diagnostic);
    CommunityLotDocument document;document.identity.house=58;document.identity.mode=4;
    document.identity.neighborhood_sha256.fill(3);document.identity.lot_sha256.fill(4);
    document.architecture.prepared.width=document.architecture.prepared.height=4;
    document.architecture.prepared.buildable.assign(16,1);
    for(auto& level:document.architecture.prepared.levels){level.floors.assign(16,0);level.walls.resize(16);}
    document.objects=decode_actorless_object_trade_state(bought.archive,&identity);
    const auto document_bytes=encode_session_archive(encode_community_lot_document(document));
    const auto loaded_document=decode_community_lot_document(decode_session_archive(document_bytes));
    require(loaded_document.objects&&same_community_lot(document.identity,loaded_document.identity),
        "typed community document reopens architecture plus exact actorless lifecycle state");
    auto reopened=*loaded_document.objects;
    const auto before=encode_actorless_object_trade_state(reopened);
    auto scene=prepare_community_object_scene(original,reopened,index,resources);
    require(scene.runtime_objects.size()==1&&scene.runtime_groups.size()==1&&scene.physical_groups.size()==1&&
        scene.physical_groups.front().complete&&scene.saved_world->records.empty()&&
        scene.saved_world->world->find(1)->object_words.at(0)==88&&
        scene.saved_world->world->find(1)->object_words.at(6)==11&&
        scene.saved_world->world->find(1)->object_words.at(43)==10&&
        reopened.object_threads.front().vm.steps==0&&encode_actorless_object_trade_state(reopened)==before,
        "scene reopens actual Init/EP8/EP11 effects and staged Main without executing/resetting either state");
    const auto rejects=[&](const ActorlessObjectTradeState& candidate,const char* reason){
        bool rejected=false;try{(void)prepare_community_object_scene(original,candidate,index,resources);}
        catch(const FormatError&){rejected=true;}require(rejected,reason);
    };
    auto wrong=reopened;wrong.object_threads.front().resource_owners.front().tree_table_id^=1;
    rejects(wrong,"reopen rejects changed exact resource tree table");
    wrong=reopened;wrong.object_threads.clear();wrong.scheduler.entities.clear();
    rejects(wrong,"reopen cannot manufacture missing created lifecycle Thread");
    wrong=reopened;wrong.created_objects.front().source="other.iff";
    rejects(wrong,"reopen rejects creation provenance from another asset");
    command.kind=ObjectTradeSessionCommandKind::move;command.target={1,reopened.world.entities.front().generation};
    command.position={40,24,1,0};services.groups=scene.physical_groups;
    auto moved=prepare_community_object_command(resources,reopened,prepared,raw,rooms,command,services);
    require(moved.status==ObjectTradeTransactionStatus::prepared_success&&moved.candidate,
        "actorless scene supplies complete physical authority for move: "+moved.diagnostic+
        " decision="+std::to_string(static_cast<unsigned>(moved.decision.reason)));
    auto moved_scene=prepare_community_object_scene(original,*moved.candidate,index,resources);
    require(moved_scene.saved_world->world->find(1)->position->x==40,"scene retains actual moved position");
    command.kind=ObjectTradeSessionCommandKind::sell;services.groups=moved_scene.physical_groups;
    const auto sold=prepare_community_object_command(resources,*moved.candidate,prepared,raw,rooms,command,services);
    require(sold.status==ObjectTradeTransactionStatus::prepared_success&&sold.candidate,
        "actorless scene supplies complete physical authority for sale: "+sold.diagnostic+
        " decision="+std::to_string(static_cast<unsigned>(sold.decision.reason)));
    auto sold_scene=prepare_community_object_scene(original,*sold.candidate,index,resources);
    require(sold_scene.runtime_objects.empty()&&sold_scene.physical_groups.empty()&&
        sold.candidate->history.retired_created_objects.size()==1&&original.world->snapshot().entities.empty(),
        "sale reopens empty scene, authenticates retired provenance and preserves immutable original");
    require(original.world->create(10,EntityKind::object)&&original.world->define_type(10,0x1234)&&
        original.world->create(11,EntityKind::object)&&original.world->define_type(11,0x1234),"original group fixture");
    original.multitile_groups.push_back({10,std::nullopt,{10,11}});
    auto grouped=make_community_object_baseline(original,identity,globals,std::nullopt,1);
    auto grouped_scene=prepare_community_object_scene(original,grouped,index,resources);
    require(grouped.history.reserved_entity_ids.empty()&&grouped_scene.physical_groups.size()==1&&
        grouped_scene.physical_groups.front().members.size()==2&&grouped_scene.saved_world->multitile_groups.size()==1,
        "original heterogeneous physical membership remains complete without invented common master");
    command.kind=ObjectTradeSessionCommandKind::move;command.target={10,original.world->find(10)->generation};
    services.groups=grouped_scene.physical_groups;
    const auto unrepresented=prepare_community_object_command(resources,grouped,prepared,raw,rooms,command,services);
    require(unrepresented.status==ObjectTradeTransactionStatus::unsupported&&!unrepresented.candidate&&
        unrepresented.diagnostic.find("no authenticated frozen Thread")!=std::string::npos,
        "legacy originals cannot move by inventing a Thread or replaying a fresh Init");
    wrong=grouped;wrong.world.entities.front().generation=++wrong.world.generation_sequence;
    rejects(wrong,"numeric ID reuse cannot borrow original render/group authority");
}
void multipart_trade_matrix(const std::filesystem::path& root){
    for(const std::size_t count:{2U,3U}){
        std::string stage="resource plan";
        try{
        const auto game=root/("group-game-"+std::to_string(count));std::filesystem::create_directories(game);
        const auto object_file=game/"group.iff",global_file=game/"Global.far";
        constexpr std::uint32_t requested=0x2200;
        const auto objd=[&](std::uint32_t guid,std::uint16_t sub,std::uint16_t lead){
            std::string b(190,'\0');at(b,0,138,4);at(b,8,1,2);at(b,18,4,2);at(b,20,77,2);at(b,22,sub,2);
            at(b,28,guid,4);at(b,36,guid==requested?240:17,2);at(b,44,1,2);at(b,80,1,2);at(b,116,1,2);
            at(b,126,lead,2);at(b,134,16,2);return b;
        };
        std::string objf(16+33*4,'\0');at(objf,4,1,4);objf.replace(8,4,"fJBO");at(objf,12,33,4);
        for(const auto& ep:std::vector<std::pair<unsigned,unsigned>>{{0,4096},{1,4097},{6,4101},{9,4098},{11,4099},{12,4100}})
            at(objf,16+ep.first*4+2,ep.second,2);
        const auto marker_bhav=[](std::uint16_t opcode){std::string b;put(b,0x8002,2);put(b,1,2);b.append(8,'\0');
            put(b,opcode,2);b.push_back(char(254));b.push_back(char(255));b.append(8,'\0');return b;};
        auto init=assignments({{4,1},{13,128},{42,1},{23,2},{43,10}});
        at(init,2,6,2);init[12+4*12+2]=5;
        const auto finish=marker_bhav(200);init+=finish.substr(12);
        std::vector<std::string> chunks;std::vector<DefinitionCandidate> definitions;
        const auto add=[&](std::uint16_t chunk_id,const std::string& definition){
            chunks.push_back(chunk("OBJD",chunk_id,definition));chunks.push_back(chunk("OBJf",chunk_id,objf));
            std::istringstream input(definition,std::ios::binary);
            definitions.push_back({object_file.string(),"",chunk_id,read_object_definition(View(input,definition.size()))});
        };
        add(128,objd(requested,65535,0));
        add(130,objd(requested+1,0x0100,0)); // Authored order differs from numeric chunk order.
        add(129,objd(requested+2,0,1));      // Base is the second part.
        if(count==3)add(131,objd(requested+3,0xff00,0));
        auto other_master=objd(0x3300,65535,0);at(other_master,20,88,2);add(140,other_master);
        auto other_part=objd(0x3301,0x0100,0);at(other_part,20,88,2);add(141,other_part);
        chunks.push_back(chunk("BHAV",4096,init));chunks.push_back(chunk("BHAV",4097,assignments({{0,77}})));
        chunks.push_back(chunk("BHAV",4098,marker_bhav(201)));chunks.push_back(chunk("BHAV",4099,marker_bhav(202)));
        chunks.push_back(chunk("BHAV",4100,marker_bhav(203)));chunks.push_back(chunk("BHAV",4101,marker_bhav(204)));write(object_file,iff(chunks));
        write(global_file,far({{"global.iff",iff({chunk("BHAV",256,assignments({{0,1}}))})}}));
        DefinitionIndex index(std::move(definitions));ObjectTradeResourceLibrary resources(game,index,global_file,"global.iff");
        const auto plan=resources.load_group(requested);
        require(plan.group&&plan.group->requested&&plan.group->requested->guid==requested&&plan.group->parts.size()==count&&
            plan.group->base_index==1&&plan.group->parts[0].resource.origin.guid==requested+1&&
            plan.group->parts[1].resource.origin.guid==requested+2&&plan.group->parts[0].offset_x==16&&
            (!plan.group->parts.back().resource.group)&&resources.load(requested).catalog.price==240&&
            resources.load(requested+1).catalog.state==ObjectTradeCatalogState::missing,
            "group resource plan retains master catalog, exact authored part order and nonfirst base");
        for(const auto& part:plan.group->parts)require(part.resource.runtime.construction.status==ObjectConstructionStatus::ready&&
            part.resource.runtime.master_guid==requested&&part.resource.origin.master_guid==requested,
            "physical parts are preloaded with exact construction master without materializing abstract master");
        PreparedArchitecture prepared;prepared.width=prepared.height=6;prepared.buildable.assign(36,1);
        SavedArchitecture raw;raw.width=raw.height=6;raw.altitude=std::vector<std::array<std::uint8_t,4>>(36);
        for(unsigned level=0;level<2;++level){prepared.levels[level].floors.assign(36,1);prepared.levels[level].walls.resize(36);
            raw.levels[level].floors=prepared.levels[level].floors;raw.levels[level].walls.resize(36);}
        const auto rooms=generate_prepared_room_map(prepared);
        std::array<std::uint8_t,32> hood{};hood[0]=static_cast<std::uint8_t>(0x74+count);
        Family family;family.family_number=12;family.budget=9000;family.member_guids={123456};
        std::vector<FamilyEntry> families{{21,std::move(family)}};
        const auto overlay=plan_family_managed_move_in_overlay(families,hood,nullptr,21,7,1000);
        FamilySessionJournal move_in(families,hood,std::nullopt,7,21,overlay);move_in.commit_move_in();
        FamilySessionJournal journal(families,hood,overlay,7,21);auto live=source();live.family_lease=journal.lease();
        live.architecture_object_support=make_architecture_object_support(6,6);
        auto allocation_history=*live.object_history;allocation_history.reserved_entity_ids={2,4};
        require(allocate_object_trade_group_ids(live.world,allocation_history,3)==std::vector<std::int16_t>{3,5,6},
            "one allocator handles complete group order across reserved gaps without reusing live IDs");
        const auto empty=std::make_shared<const VmProgram>(RoutineSet{});
        std::vector<std::pair<std::uint16_t,std::int16_t>> events;bool fail_ep11=false;unsigned init_count=0;
        ObjectTradeSessionHooks hooks;hooks.capture_current=[&]{return live;};
        hooks.validate_reopen=[](const SessionBundle& candidate,const std::vector<std::uint8_t>& bytes){
            require(encode_session_bundle(candidate)==bytes&&encode_session_bundle(decode_session_bundle(bytes))==bytes,
                "group transaction first archive and complete candidate reopen exactly");
        };
        hooks.physical_groups=[&]{std::vector<ObjectTradeGroup> result;std::set<std::int16_t> assigned;
            if(live.object_groups)for(const auto& group:live.object_groups->groups){ObjectTradeGroup membership;membership.complete=true;
                for(const auto& member:group.members){membership.members.push_back({member.id,member.generation});assigned.insert(member.id);}result.push_back(std::move(membership));}
            for(const auto& e:live.world.entities)if(e.kind==EntityKind::object&&!assigned.count(e.id))result.push_back({true,{{e.id,e.generation}}});return result;
        };
        hooks.thread_programs=[&](auto id,const SessionBundle& source){ObjectTradeThreadPrograms result;result.globals=resources.global_program();
            if(id==1){result.primary_owner=123456;result.owners.emplace(123456,empty);return result;}
            const auto thread=std::find_if(source.object_threads.begin(),source.object_threads.end(),[&](const auto& item){return item.id==id;});
            require(thread!=source.object_threads.end(),"group actual Thread seed exists");result.primary_owner=thread->primary_owner;
            const auto* record=resources.find(thread->guid);require(record,"group exact private code preloaded");
            result.owners.emplace(result.primary_owner,std::make_shared<const VmProgram>(record->code.runtime.private_routines));return result;
        };
        hooks.extra_primitive_factory=[&](const ObjectTradeCandidateContext& ctx,const VmMemory&){
            auto* world=&ctx.world;const auto thread_for=ctx.thread_for;
            return [&,world,thread_for](const VmFrame& frame,const Instruction& instruction,const Ts1Temps&,const VmDispatch&)->PrimitiveOutcome{
                if(instruction.opcode<200||instruction.opcode>204)return {};
                events.emplace_back(instruction.opcode,frame.context.caller);
                if(instruction.opcode==200){
                    std::size_t objects=0;
                    for(std::int16_t cursor=0;;){const auto next=world->next_entity(cursor);if(!next)break;cursor=*next;
                        if(world->find(cursor)->kind==EntityKind::object){++objects;require(thread_for(cursor)&&thread_for(cursor)->steps()==0,
                            "all member Worlds and real Threads are available before first Init without Main ticks");}}
                    require(objects==count,"Init can discover every physical sibling before initialization");
                    require(world->read(frame,3,41)==static_cast<std::int16_t>(init_count?100+init_count:240),
                        "Init observes same shared master catalog price then preceding part CurrentValue write");
                    ++init_count;require(world->write(frame,3,41,static_cast<std::int16_t>(100+init_count)),"Init changes shared group value");
                }
                if(instruction.opcode==202&&fail_ep11)return {PrimitiveDisposition::unsupported,"injected group EP11 failure"};
                if(instruction.opcode==203)require(world->write(frame,3,41,1),"EP12 writes value only after refund amount was captured");
                return {PrimitiveDisposition::goto_true,{}};
            };
        };
        ObjectTradeSessionAdapter adapter(resources,journal,prepared,raw,rooms,hooks);
        ObjectTradeSessionCommand command;command.kind=ObjectTradeSessionCommandKind::purchase;command.guid=requested;command.position={40,40,1,0};
        stage="household purchase";
        auto result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,
            "complete group purchase: "+result.diagnostic);live=decode_session_bundle(result.archive);
        require(live.object_groups&&live.object_groups->groups.size()==1&&live.object_groups->groups.front().members.size()==count&&
            live.object_threads.size()==count&&live.object_trade&&live.object_trade->groups.size()==1&&
            live.object_trade->groups.front().members.size()==count&&live.object_trade->groups.front().initial_price==240&&
            live.object_trade->groups.front().price_origin.kind==SessionObjectPriceSource::purchase&&*live.host.family_budget==7760,
            "one catalog debit buys all pieces, one physical/value group and each real Thread");
        const auto physical=live.object_groups->groups.front();require(physical.base_id==physical.members[1].id,"purchase preserves nonfirst source base");
        std::vector<std::int16_t> initializers,placements;
        for(const auto& event:events){if(event.first==200)initializers.push_back(event.second);if(event.first==202)placements.push_back(event.second);}
        std::vector<std::int16_t> order;for(const auto& part:physical.members)order.push_back(part.id);
        require(initializers==order&&placements==order,"Init and user-placement execute each member in authored order");
        stage="household rotations";
        for(const auto direction:{0U,2U,4U,6U}){
            events.clear();command.kind=ObjectTradeSessionCommandKind::move;command.target={physical.members[0].id,physical.members[0].generation};
            command.position={56,56,1,static_cast<std::uint8_t>(direction)};command.move_objects=true;
            result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"complete group rotation: "+result.diagnostic);
            live=decode_session_bundle(result.archive);require(*live.host.family_budget==7760&&live.object_trade->groups.front().initial_price==240,
                "group movement does not reprice or debit furniture");
            for(const auto& part:physical.members){const auto entity=std::find_if(live.world.entities.begin(),live.world.entities.end(),[&](const auto& e){return e.id==part.id;});
                int dx=part.offset_x,dy=part.offset_y;const auto old_dx=dx;
                switch(direction){case 2:dx=-dy;dy=old_dx;break;case 4:dx=-dx;dy=-dy;break;case 6:dx=dy;dy=-old_dx;break;default:break;}
                require(entity!=live.world.entities.end()&&entity->generation==part.generation&&
                    entity->position==std::optional<EntityPosition>{{56+dx,56+dy,1,static_cast<std::uint8_t>(direction)}},
                    "every physical part follows authenticated source base offsets at each cardinal rotation");}
            placements.clear();for(const auto& event:events)if(event.first==202)placements.push_back(event.second);
            require(placements==order,"successful movement runs user placement on all parts");
        }
        if(count==3){
            stage="late movement refusal";
            const auto previous=live;events.clear();command.position={8,8,1,0};
            result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,
                "authored late group placement refusal still produces its coherent checkpoint: "+result.diagnostic);
            live=decode_session_bundle(result.archive);
            require(std::none_of(events.begin(),events.end(),[](const auto& e){return e.first==202;})&&*live.host.family_budget==7760,
                "late failed group move runs no user-placement action and keeps money");
            for(const auto id:order){const auto before=std::find_if(previous.world.entities.begin(),previous.world.entities.end(),[&](const auto& e){return e.id==id;});
                const auto after=std::find_if(live.world.entities.begin(),live.world.entities.end(),[&](const auto& e){return e.id==id;});
                require(before!=previous.world.entities.end()&&after!=live.world.entities.end()&&before->position==after->position,
                    "late refusal restores each earlier part's original physical position");}
        }
        stage="technical rollback and household sale";
        const auto stable=encode_session_bundle(live);const auto head=journal.checkpoint_path();fail_ep11=true;
        command.position={56,56,1,0};result=adapter.execute(command);fail_ep11=false;
        require(result.state==ObjectTradeSessionState::unsupported&&journal.checkpoint_path()==head&&encode_session_bundle(live)==stable,
            "group script failure leaves full source archive and journal head unchanged");
        events.clear();command.kind=ObjectTradeSessionCommandKind::sell;
        result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"complete group sale: "+result.diagnostic);
        live=decode_session_bundle(result.archive);
        require(*live.host.family_budget==8000&&live.created_objects.empty()&&live.object_threads.empty()&&
            (!live.object_groups||live.object_groups->groups.empty())&&live.object_trade->groups.empty()&&live.world.entities.size()==1&&
            live.object_history->retired_created_objects.size()==count,
            "sale retires complete static group and its Threads/history, refunding only once before EP12 price write");
        require(events==std::vector<std::pair<std::uint16_t,std::int16_t>>{{203,physical.members[0].id}},
            "EP12 runs only on clicked nonbase member and executable EP6 is suppressed during Dead removal");
        if(count==2){
            stage="heterogeneous source preparation";
            // Synthetic original-source shape: unlike constructed OGRP, an
            // imported physical group may contain pieces from different masters.
            events.clear();init_count=0;command.kind=ObjectTradeSessionCommandKind::purchase;
            command.guid=requested;command.position={40,40,1,0};
            result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"import-shape fixture preparation: "+result.diagnostic);
            live=decode_session_bundle(result.archive);auto manifest=live.object_groups->groups.front();
            const auto replaced=manifest.members.front().id;
            auto extra=resources.command_resource(0x3301);extra.runtime.master_guid=0x3300;
            World imported_world;require(imported_world.restore_snapshot(live.world)&&imported_world.remove(replaced),"prepare exact heterogeneous source body");
            require(materialize_runtime_object(imported_world,replaced,extra.runtime)==ObjectRuntimeCreateStatus::created,
                "heterogeneous source uses its own real OBJD and layout");
            const auto new_generation=imported_world.find(replaced)->generation;
            for(const auto field:std::vector<std::pair<std::int16_t,std::int16_t>>{{4,1},{13,128},{42,1},{23,2},{43,10}})
                require(imported_world.declare_object_word(replaced,field.first,field.second),"heterogeneous original source movement flags");
            const auto old=std::find_if(live.world.entities.begin(),live.world.entities.end(),[&](const auto& e){return e.id==replaced;});
            require(old!=live.world.entities.end()&&old->position&&imported_world.set_position(replaced,*old->position),"heterogeneous source original position");
            Vm imported_vm(extra.global_routines,imported_world.vm_memory(),{}, {},RoutineOwners{{0x3301,extra.runtime.private_routines}});
            Sleep imported_sleep(imported_world);require(imported_sleep.bind(imported_vm)&&
                imported_vm.start(routine_key(4097),{replaced,replaced,0,0x3301},{0,0,0,0},true)==VmStatus::running,
                "heterogeneous original source has its own bound Main without execution");
            require(imported_sleep.join_at(live.host.tick,replaced),"heterogeneous source Sleep binds exact actor generation and scheduler clock");
            auto thread=std::find_if(live.object_threads.begin(),live.object_threads.end(),[&](const auto& entry){return entry.id==replaced;});
            require(thread!=live.object_threads.end()&&thread->resource_owners.size()==1,"source physical part has one exact private manifest");
            thread->generation=new_generation;thread->guid=0x3301;thread->primary_owner=0x3301;
            thread->vm=imported_vm.snapshot();thread->sleep=imported_sleep.snapshot();
            auto& owner=thread->resource_owners.front();owner.owner=owner.resource_guid=0x3301;owner.definition_chunk_id=141;
            for(auto& scheduled:live.scheduler.entities)if(scheduled.id==replaced){scheduled.status=imported_vm.status();
                scheduled.steps=imported_vm.steps();scheduled.run_generation=imported_vm.run_generation();}
            manifest.definition_guid=0;manifest.members.front().generation=new_generation;
            auto& member=live.object_trade->groups.front().members.front();member.object.id=replaced;member.object.generation=new_generation;
            member.object.guid=0x3301;member.object.definition_chunk_id=141;member.object.master_guid=0x3300;
            live.created_objects.clear();live.object_groups.reset();
            {
                ObjectValueRegistry values(imported_world,*live.object_trade);require(values.bind(),"bind imported group shared state");
                VmFrame frame;frame.context.caller=replaced;
                require(imported_world.write(frame,3,41,240),"actual CurrentValue write changes price origin without changing physical group");
            }
            live.world=imported_world.snapshot();
            require(live.object_trade->groups.front().price_origin.guid==0x3301&&
                live.object_trade->groups.front().price_origin.kind==SessionObjectPriceSource::current_value_write,
                "mutable price origin differs from both physical masters");
            stage="heterogeneous source checkpoint and commands";
            const auto original_import=encode_session_bundle(live);
            auto imported_hooks=hooks;bool wrong_generation=false;
            imported_hooks.physical_groups=[&]{ObjectTradeGroup group;group.complete=true;
                for(const auto& m:manifest.members)group.members.push_back({m.id,m.generation});return std::vector<ObjectTradeGroup>{group};};
            imported_hooks.physical_group=[&](ObjectPositionIdentity id)->std::optional<ObjectGroup>{
                if(std::none_of(manifest.members.begin(),manifest.members.end(),[&](const auto& m){return m.id==id.id&&m.generation==id.generation;}))return {};
                auto copy=manifest;if(wrong_generation)++copy.members.back().generation;return copy;
            };
            ObjectTradeSessionAdapter imported_adapter(resources,journal,prepared,raw,rooms,imported_hooks);
            command.kind=ObjectTradeSessionCommandKind::move;command.target={replaced,new_generation};command.position={56,56,1,2};command.move_objects=true;
            const auto old_head=journal.checkpoint_path();wrong_generation=true;result=imported_adapter.execute(command);wrong_generation=false;
            require(result.state==ObjectTradeSessionState::unsupported&&journal.checkpoint_path()==old_head&&encode_session_bundle(live)==original_import,
                "imported physical manifest cannot substitute a stale sibling or publish altered source");
            result=imported_adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,
                "heterogeneous original physical group movement: "+result.diagnostic);live=decode_session_bundle(result.archive);
            require(!live.object_groups&&live.object_trade->groups.front().members.front().object.master_guid==0x3300&&
                live.object_trade->groups.front().members.back().object.master_guid==requested&&*live.host.family_budget==7760,
                "imported movement preserves individual masters without fabricating OGRP or using price-origin GUID");
            events.clear();command.kind=ObjectTradeSessionCommandKind::sell;result=imported_adapter.execute(command);
            require(result.state==ObjectTradeSessionState::published_reload_required,"heterogeneous original group sale: "+result.diagnostic);
            live=decode_session_bundle(result.archive);
            require(live.world.entities.size()==1&&live.object_trade->groups.empty()&&live.object_threads.empty()&&
                live.removed_original_objects.size()==count&&*live.host.family_budget==8000,
                "heterogeneous source sale removes complete original membership and refunds group once");
        }
        if(count==2){
            stage="actorless consumer and document/scene reopens";
            SavedWorld original;original.world=std::make_unique<World>();original.relationships=std::make_unique<RelationshipState>();
            SessionResourceIdentity identity;identity.resource_fingerprint.fill(0x65);
            auto global_state=RuntimeGlobals::from_legacy({}).snapshot();global_state.words[32]=1;
            auto frozen=make_community_object_baseline(original,identity,global_state,std::nullopt,17);
            CommunityObjectCommandServices services;services.source_still_matches=[]{return true;};
            services.bindings.extra_primitive_factory=hooks.extra_primitive_factory;
            events.clear();init_count=0;command.kind=ObjectTradeSessionCommandKind::purchase;command.guid=requested;command.position={40,40,1,0};
            auto bought=prepare_community_object_command(resources,frozen,prepared,raw,rooms,command,services);
            require(bought.status==ObjectTradeTransactionStatus::prepared_success&&bought.candidate,
                "real community consumer buys complete group without household: "+bought.diagnostic);
            const auto components=actorless_object_trade_components(*bought.candidate);
            require(components.object_groups&&components.object_groups->groups.size()==1&&components.object_groups->groups.front().members.size()==count&&
                !components.host.family_budget&&!components.host.runtime_avatar_id&&components.runtime_entities.empty()&&
                components.object_threads.size()==count&&components.object_trade->groups.size()==1,
                "actorless group preserves OGRP/value/Thread ownership without inventing resident, family or money");
            CommunityLotDocument document;document.identity.house=58;document.identity.mode=4;
            document.identity.neighborhood_sha256.fill(3);document.identity.lot_sha256.fill(4);
            document.architecture.prepared.width=document.architecture.prepared.height=6;document.architecture.prepared.buildable.assign(36,1);
            for(auto& level:document.architecture.prepared.levels){level.floors.assign(36,0);level.walls.resize(36);}
            document.objects=decode_actorless_object_trade_state(bought.archive,&identity);
            const auto bytes=encode_session_archive(encode_community_lot_document(document));
            auto reopened_document=decode_community_lot_document(decode_session_archive(bytes));
            require(reopened_document.objects&&encode_session_archive(encode_community_lot_document(reopened_document))==bytes,
                "real community document reopens complete actorless group and architecture exactly");
            frozen=*reopened_document.objects;const auto frozen_bytes=encode_actorless_object_trade_state(frozen);
            auto scene=prepare_community_object_scene(original,frozen,index,resources);
            require(scene.runtime_objects.size()==count&&scene.runtime_groups.size()==1&&scene.runtime_groups.front().members.size()==count&&
                scene.physical_groups.size()==1&&scene.physical_groups.front().members.size()==count&&
                encode_actorless_object_trade_state(frozen)==frozen_bytes,
                "frozen scene restores all rendered parts as one exact group without Main execution");
            command.kind=ObjectTradeSessionCommandKind::move;
            const auto selected=scene.runtime_groups.front().members.front();command.target={selected.id,selected.generation};
            command.position={56,56,1,2};command.move_objects=true;services.groups=scene.physical_groups;
            auto moved=prepare_community_object_command(resources,frozen,prepared,raw,rooms,command,services);
            require(moved.status==ObjectTradeTransactionStatus::prepared_success&&moved.candidate,
                "real community consumer moves complete group: "+moved.diagnostic);
            auto moved_scene=prepare_community_object_scene(original,*moved.candidate,index,resources);
            require(moved_scene.runtime_groups.size()==1&&moved_scene.runtime_objects.size()==count,
                "community moved group remains physically complete on scene reopening");
            command.kind=ObjectTradeSessionCommandKind::sell;services.groups=moved_scene.physical_groups;events.clear();
            const auto sold=prepare_community_object_command(resources,*moved.candidate,prepared,raw,rooms,command,services);
            require(sold.status==ObjectTradeTransactionStatus::prepared_success&&sold.candidate,
                "real community consumer sells complete group: "+sold.diagnostic);
            auto empty_scene=prepare_community_object_scene(original,*sold.candidate,index,resources);
            require(empty_scene.runtime_objects.empty()&&empty_scene.runtime_groups.empty()&&empty_scene.physical_groups.empty()&&
                sold.candidate->world.entities.empty()&&sold.candidate->trade.groups.empty()&&
                sold.candidate->history.retired_created_objects.size()==count&&original.world->snapshot().entities.empty(),
                "actorless group sale leaves no pieces and preserves immutable original source");
        }
        if(count==3){
            stage="authored full-group purchase refusal";
            events.clear();init_count=0;command.kind=ObjectTradeSessionCommandKind::purchase;command.guid=requested;command.position={8,8,1,0};
            result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required&&
                result.transaction==ObjectTradeTransactionStatus::prepared_authored_refusal,"late refused purchase records full original cleanup: "+result.diagnostic);
            live=decode_session_bundle(result.archive);
            require(*live.host.family_budget==8000&&live.world.entities.size()==1&&live.created_objects.empty()&&
                live.object_threads.empty()&&live.object_trade->groups.empty()&&live.object_history->retired_created_objects.size()==2*count,
                "refused full-group purchase removes all constructed parts and refunds master price once");
            require(std::count_if(events.begin(),events.end(),[](const auto& e){return e.first==202;})==static_cast<std::ptrdiff_t>(count)&&
                std::none_of(events.begin(),events.end(),[](const auto& e){return e.first==203;}),
                "purchase refusal still runs user placement for every part but does not invent a sale EP12");
        }
        }catch(const std::exception& error){
            throw std::runtime_error("multipart parts="+std::to_string(count)+" stage="+stage+": "+error.what());
        }
    }
}
void dynamic_association_matrix(const std::filesystem::path& root){
    const auto game=root/"dynamic-game";std::filesystem::create_directories(game);
    const auto object_file=game/"dynamic.iff",global_file=game/"Global.far";
    std::string objd(190,'\0');at(objd,0,138,4);at(objd,8,1,2);at(objd,18,4,2);at(objd,28,0x9911,4);at(objd,36,100,2);at(objd,44,1,2);at(objd,80,1,2);at(objd,116,1,2);at(objd,134,16,2);
    std::string objf(16+33*4,'\0');at(objf,4,1,4);objf.replace(8,4,"fJBO");at(objf,12,33,4);
    for(const auto ep:std::vector<std::pair<unsigned,unsigned>>{{0,4096},{1,4097},{8,4100},{11,4101},{12,4102}})at(objf,16+ep.first*4+2,ep.second,2);
    const auto marker=[](std::uint16_t opcode){std::string b;put(b,0x8002,2);put(b,1,2);b.append(8,'\0');put(b,opcode,2);b.push_back(char(254));b.push_back(char(255));b.append(8,'\0');return b;};
    write(object_file,iff({chunk("OBJD",128,objd),chunk("OBJf",128,objf),chunk("BHAV",4096,assignments({{4,1},{13,128},{42,1},{23,2},{43,10}})),chunk("BHAV",4097,assignments({{0,77}})),chunk("BHAV",4100,marker(200)),chunk("BHAV",4101,marker(201)),chunk("BHAV",4102,marker(202))}));
    write(global_file,far({{"global.iff",iff({chunk("BHAV",256,assignments({{0,1}}))})}}));
    std::istringstream input(objd,std::ios::binary);DefinitionIndex index({{object_file.string(),"",128,read_object_definition(View(input,objd.size()))}});ObjectTradeResourceLibrary resources(game,index,global_file,"global.iff");
    PreparedArchitecture prepared;prepared.width=prepared.height=8;prepared.buildable.assign(64,1);SavedArchitecture raw;raw.width=raw.height=8;raw.altitude=std::vector<std::array<std::uint8_t,4>>(64);
    for(unsigned level=0;level<2;++level){prepared.levels[level].floors.assign(64,1);prepared.levels[level].walls.resize(64);raw.levels[level].floors=prepared.levels[level].floors;raw.levels[level].walls.resize(64);}
    const auto rooms=generate_prepared_room_map(prepared);SavedWorld original;original.world=std::make_unique<World>();original.relationships=std::make_unique<RelationshipState>();SessionResourceIdentity identity;identity.resource_fingerprint.fill(0x19);
    for(const unsigned direction:{0U,2U,4U,6U}){
        std::string stage="baseline";try{
        auto simless=RuntimeGlobals::from_legacy({}).snapshot();simless.words[32]=1;
        auto state=make_community_object_baseline(original,identity,simless,std::nullopt,7);
        CommunityObjectCommandServices services;bool authority=true,fail_user=false;services.source_still_matches=[&]{return authority;};
        std::vector<std::pair<std::int16_t,std::int16_t>> flag_events;std::vector<std::int16_t> placements,deletions;
        services.bindings.extra_primitive_factory=[&](const ObjectTradeCandidateContext&ctx,const VmMemory&){auto*world=&ctx.world;return [&,world](const VmFrame&frame,const Instruction&i,const Ts1Temps&,const VmDispatch&)->PrimitiveOutcome{
            if(i.opcode==200){require(frame.arguments.size()==4,"dynamic EP8 exact argument extent");flag_events.emplace_back(frame.context.caller,frame.arguments[0]);return {PrimitiveDisposition::goto_true,{}};}
            if(i.opcode==201){placements.push_back(frame.context.caller);return fail_user?PrimitiveOutcome{PrimitiveDisposition::unsupported,"injected dynamic EP11 fault"}:PrimitiveOutcome{PrimitiveDisposition::goto_true,{}};}
            if(i.opcode==202){deletions.push_back(frame.context.caller);require(world->write(frame,3,41,9),"dynamic EP12 changes shared value after captured refund");return {PrimitiveDisposition::goto_true,{}};}return {};};};
        auto roundtrip=[&](ActorlessObjectTradeState candidate){
            std::cout<<"dynamic direction="<<direction<<" stage="<<stage<<" OGRP=";
            if(candidate.object_groups)for(const auto&g:candidate.object_groups->groups){std::cout<<g.base_id<<'[';for(const auto&m:g.members)std::cout<<m.id<<',';std::cout<<"]";}
            std::cout<<" OTRD=";for(const auto&g:candidate.trade.groups){std::cout<<(g.base?g.base->id:0)<<'[';for(const auto&m:g.members)std::cout<<m.object.id<<',';std::cout<<"]";}std::cout<<std::endl;
            CommunityLotDocument doc;doc.identity.house=58;doc.identity.mode=4;doc.identity.neighborhood_sha256.fill(3);doc.identity.lot_sha256.fill(4);doc.architecture.prepared=prepared;doc.objects=std::move(candidate);
            const auto bytes=encode_session_archive(encode_community_lot_document(doc));auto loaded=decode_community_lot_document(decode_session_archive(bytes));require(loaded.objects.has_value(),"dynamic actorless document retains required group section");auto restored=*loaded.objects;
            const auto scene=prepare_community_object_scene(original,restored,index,resources);require(scene.runtime_objects.size()==restored.created_objects.size(),"dynamic scene binds each exact per-member resource after document reopen");services.groups=scene.physical_groups;return restored;};
        ObjectTradeSessionCommand cmd;cmd.kind=ObjectTradeSessionCommandKind::purchase;cmd.guid=0x9911;
        for(const int x:{24,40,56}){stage="purchase x="+std::to_string(x);placements.clear();cmd.position={x,40,1,static_cast<std::uint8_t>(direction)};auto bought=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);require(bought.candidate&&bought.status==ObjectTradeTransactionStatus::prepared_success,"dynamic adjacent purchase: "+bought.diagnostic);state=roundtrip(*bought.candidate);}
        require(state.object_groups&&state.object_groups->groups.size()==1&&state.object_groups->groups[0].kind==ObjectGroupKind::live_association&&state.object_groups->groups[0].members.size()==3&&state.object_groups->groups[0].members[0].id==3&&state.object_groups->groups[0].members[1].id==2&&state.object_groups->groups[0].members[2].id==1,"new receiver keeps authored append order of the prior two-member association");
        require(std::set<std::int16_t>(placements.begin(),placements.end())==std::set<std::int16_t>{1,2,3},"purchase EP11 reaches all resulting donor members");
        const auto last_flags=[&](std::int16_t id){for(auto p=flag_events.rbegin();p!=flag_events.rend();++p)if(p->first==id)return p->second;return std::int16_t{-1};};
        const int east=(2-static_cast<int>(direction)+8)%8,west=(6-static_cast<int>(direction)+8)%8;
        require(last_flags(1)==(1<<east)&&last_flags(2)==((1<<east)|(1<<west))&&last_flags(3)==(1<<west),"EP8 adjacency flags rotate relative to the actual heading in all four directions");
        {
            stage="unknown lead rejection";auto unknown=state;unknown.trade.groups.front().members.back().lead_object.reset();
            const auto unchanged=encode_object_group_snapshot(*unknown.object_groups);const auto old_values=unknown.trade;World world;require(world.restore_snapshot(unknown.world),"unknown-base negative restores exact World");
            ObjectValueRegistry values(world,unknown.trade);DynamicObjectGroupServices callbacks;callbacks.authority=[]{return true;};
            DynamicObjectGroups dynamic(world,values,*unknown.object_groups,callbacks);const auto&m=unknown.object_groups->groups.front().members[1];
            require(!dynamic.split_for_move({m.id,m.generation})&&encode_object_group_snapshot(*unknown.object_groups)==unchanged&&unknown.trade.revision==old_values.revision&&unknown.trade.groups.size()==old_values.groups.size()&&
                unknown.trade.groups.front().members.size()==old_values.groups.front().members.size()&&unknown.trade.groups.front().base->id==old_values.groups.front().base->id&&unknown.trade.groups.front().initial_price==old_values.groups.front().initial_price&&
                !unknown.trade.groups.front().members.back().lead_object,
                "unknown source metadata rejects before physical bytes, ledger revision, member extent, base or value can change");
        }
        stage="expired authority";const auto before=encode_actorless_object_trade_state(state);authority=false;cmd.kind=ObjectTradeSessionCommandKind::move;cmd.target={2,state.object_groups->groups[0].members[1].generation};cmd.position={96,64,1,static_cast<std::uint8_t>(direction)};
        const auto expired=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);authority=true;require(!expired.candidate&&encode_actorless_object_trade_state(state)==before,"expired move authority cannot split the source association");
        stage="middle split";placements.clear();auto split=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);require(split.candidate&&split.status==ObjectTradeTransactionStatus::prepared_success,"dynamic non-base middle split: "+split.diagnostic);state=roundtrip(*split.candidate);
        require(state.object_groups->groups.size()==2&&state.trade.groups.size()==2&&placements==std::vector<std::int16_t>{2},"SetPosition separates only clicked member and executes EP11 on its resulting singleton");
        const auto old=std::find_if(state.object_groups->groups.begin(),state.object_groups->groups.end(),[](const auto&g){return g.members.size()==2;});require(old!=state.object_groups->groups.end()&&old->members[0].id==3&&old->members[1].id==1,"bridge removal does not divide remaining disconnected members or reorder them");
        for(const auto&g:state.trade.groups)require(g.initial_price==100,"split copies shared InitialPrice without debiting or summing");
        // Technical failure must not publish even though separation/combination happened privately.
        stage="technical rollback";const auto stable=encode_actorless_object_trade_state(state);fail_user=true;cmd.position={40,40,1,static_cast<std::uint8_t>(direction)};auto failed=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);fail_user=false;require(!failed.candidate&&encode_actorless_object_trade_state(state)==stable,"dynamic EP11 technical fault cannot publish candidate topology");
        stage="recombine";placements.clear();auto joined=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);require(joined.candidate&&joined.status==ObjectTradeTransactionStatus::prepared_success,"dynamic recombine after split: "+joined.diagnostic);state=roundtrip(*joined.candidate);require(state.object_groups->groups.size()==1&&std::set<std::int16_t>(placements.begin(),placements.end())==std::set<std::int16_t>{1,2,3},"move EP11 executes on moved member and newly joined donor threads");
        // Invalid destination is authored refusal: separation remains in the candidate.
        stage="authored refusal";placements.clear();cmd.position={-8,40,1,static_cast<std::uint8_t>(direction)};auto refused=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);require(refused.candidate&&refused.status==ObjectTradeTransactionStatus::prepared_authored_refusal,"dynamic refused placement still captures split: "+refused.diagnostic);state=roundtrip(*refused.candidate);require(state.object_groups->groups.size()==1&&state.object_groups->groups.front().members.size()==3&&placements.empty()&&
            std::find_if(state.world.entities.begin(),state.world.entities.end(),[](const auto&e){return e.id==2;})->position==std::optional<EntityPosition>{{40,40,1,static_cast<std::uint8_t>(direction)}},
            "known out-of-bounds refusal restores the old pose; Post legitimately reunites adjacent old peers without running EP11");
        // Distinct source early floor guard: Stories is known5, so level6
        // returns before Pre/Post. The split survives; only the old members
        // receive flags, and the moved member keeps its old pose/last EP8.
        stage="early story refusal";placements.clear();flag_events.clear();cmd.position={40,40,6,static_cast<std::uint8_t>(direction)};
        auto early=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);
        require(early.candidate&&early.status==ObjectTradeTransactionStatus::prepared_authored_refusal,
            "source known Stories guard still publishes prior separation: "+early.diagnostic);state=roundtrip(*early.candidate);
        require(state.object_groups->groups.size()==2&&state.trade.groups.size()==2&&placements.empty()&&
            std::none_of(flag_events.begin(),flag_events.end(),[](const auto&e){return e.first==2;})&&last_flags(1)==0&&last_flags(3)==0,
            "early Stories refusal preserves split and updates only the old association without Post or EP11");
        stage="reunite after refusal";cmd.position={40,40,1,static_cast<std::uint8_t>(direction)};joined=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);require(joined.candidate.has_value(),"dynamic reunite after refusal: "+joined.diagnostic);state=roundtrip(*joined.candidate);
        stage="selected-only sale";deletions.clear();cmd.kind=ObjectTradeSessionCommandKind::sell;auto sold=prepare_community_object_command(resources,state,prepared,raw,rooms,cmd,services);require(sold.candidate&&sold.status==ObjectTradeTransactionStatus::prepared_success,"dynamic selected-only sale: "+sold.diagnostic);state=roundtrip(*sold.candidate);
        require(deletions==std::vector<std::int16_t>{2}&&state.created_objects.size()==2&&state.object_threads.size()==2&&state.object_groups->groups.size()==1&&state.object_groups->groups[0].members.size()==2&&state.trade.groups.size()==1&&state.trade.groups[0].initial_price==9,"dynamic sale removes only clicked bridge and preserves survivors plus authored EP12 price write");
        require(last_flags(1)==0&&last_flags(3)==0,"surviving disconnected members receive actual zero adjacency flags");
        std::cout<<"dynamic association direction="<<direction<<" purchase/split/recombine/refusal/delete/document-scene passed\n";
        }catch(const std::exception& e){throw std::runtime_error("dynamic direction="+std::to_string(direction)+" stage="+stage+": "+e.what());}
    }
}

void wall_trade_matrix(const std::filesystem::path& root){
    for(const unsigned kind:{0U,1U,2U}){
        const auto game=root/("wall-game-"+std::to_string(kind));std::filesystem::create_directories(game);
        const std::uint32_t guid=0x9100U+kind; // Synthetic identities, no commercial object exception.
        std::string objd(190,'\0');at(objd,0,138,4);at(objd,8,1,2);at(objd,18,4,2);at(objd,28,guid,4);
        at(objd,36,100,2);at(objd,44,1,2);at(objd,80,1,2);at(objd,116,1,2);at(objd,134,16,2);at(objd,138,kind,2);
        at(objd,92,kind==1?3:kind==2?22:0,2);at(objd,162,100,2);
        std::string objf(16+33*4,'\0');at(objf,4,1,4);objf.replace(8,4,"fJBO");at(objf,12,33,4);
        for(const auto entry:std::vector<std::pair<unsigned,unsigned>>{{0,4096},{1,4097},{6,4100}})at(objf,16+entry.first*4+2,entry.second,2);
        if(kind==1)at(objf,16+15*4+2,4101,2); // Actual portal EP metadata remains registry-owned.
        // Real Init sets required-front/exclusive placement and architectural class.
        const auto flags=kind==1?32768U:kind==2?16384U:0U;
        const auto file=game/"wall.iff",global=game/"Global.far";
        auto adjacency=assignments({{6,66}});if(kind==2)adjacency[14]=static_cast<char>(255); // Authored false after real write.
        auto init=assignments({{4,1},{13,1},{42,1},{23,2},{43,10},{63,2},{40,static_cast<std::uint16_t>(flags)}});
        // A test-owned extension executes in the actual candidate VM after its
        // real Init expressions. It can adversarially mutate an OLD registered
        // object; it is never an alternative placement/authority capability.
        at(init,2,8,2);init[12+6*12+2]=7;
        put(init,99,2);init.push_back(static_cast<char>(254));init.push_back(static_cast<char>(255));init.append(8,'\0');
        write(file,iff({chunk("OBJD",128,objd),chunk("OBJf",128,objf),
            chunk("BHAV",4096,init),
            chunk("BHAV",4097,assignments({{6,77}})),chunk("BHAV",4100,adjacency),chunk("BHAV",4101,assignments({{6,15}}))}));
        write(global,far({{"global.iff",iff({chunk("BHAV",256,assignments({{6,1}}))})}}));
        std::istringstream stream(objd,std::ios::binary);
        DefinitionIndex definitions({{file.string(),"",128,read_object_definition(View(stream,objd.size()))}});
        ObjectTradeResourceLibrary resources(game,definitions,global,"global.iff");
        const auto& admitted=resources.load(guid);require(admitted.wall_style.has_value(),"wall provider is read from concrete supported OBJD");
        SavedWorld original;original.world=std::make_unique<World>();original.relationships=std::make_unique<RelationshipState>();
        SessionResourceIdentity identity;identity.resource_fingerprint.fill(static_cast<std::uint8_t>(0x71+kind));
        auto globals=RuntimeGlobals::from_legacy({}).snapshot();globals.words[32]=1;
        auto state=make_community_object_baseline(original,identity,globals,std::nullopt,1234);
        PreparedArchitecture prepared;prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
        SavedArchitecture raw;raw.width=raw.height=4;raw.altitude=std::vector<std::array<std::uint8_t,4>>(16);
        for(std::size_t level=0;level<2;++level){prepared.levels[level].floors.assign(16,1);prepared.levels[level].walls.resize(16);
            raw.levels[level].floors.assign(16,1);raw.levels[level].walls.resize(16);}
        for(const auto index:{5U,6U}){prepared.levels[0].walls[index].segments=wall_top_right;raw.levels[0].walls[index].segments=wall_top_right;}
        const auto rooms=generate_prepared_room_map(prepared);
        ObjectPositionRegistryServices registry_services;registry_services.definitions=&definitions;
        registry_services.group=[](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{return std::vector<ObjectPositionIdentity>{id};};
        registry_services.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};
        registry_services.moved_self=[](auto)->std::optional<bool>{return false;};
        ObjectPositionRegistry registry(*original.world,prepared,rooms,registry_services);
        require(registry.finish_initial_load({},[]{return true;}),"empty source registry is certified by actual complete original cohort");
        ArchitectureObjectState objects;objects.object_support=make_architecture_object_support(4,4);
        for(auto& level:objects.occupied_walls)level.assign(16,0);
        objects.position_registry=registry.snapshot();objects.occupied_walls[1][15]=8; // Unrelated source history must survive.
        {
            World probe;require(probe.create_runtime_object(1,guid,{0,0,0},admitted.code.runtime.entry_points.entries),"actual wall input identity");
            require(probe.declare_object_word(1,13,1)&&probe.declare_object_word(1,63,2),"wall input exact post-Init words");
            bool current=true;auto input_owner=objects;
            SessionObjectPositionContext input;input.world=&probe;input.resources=&resources;input.prepared=&prepared;
            input.architecture=&raw;input.rooms=&rooms;input.architecture_objects=&input_owner;
            input.object_support=&input_owner.object_support;input.position_registry=&*input_owner.position_registry;
            input.source_still_matches=[&]{return current;};
            auto bound=make_session_object_position_services(input);const ObjectPositionIdentity target{1,probe.find(1)->generation};
            input_owner.occupied_walls[0][5]=2;
            auto fact=bound.placement.architecture(target,EntityPosition{24,24,1,0});
            require(fact&&fact->occupied_walls==2,"PositionValid consumes the SAME supplied AOBJ, not an empty saved wall tile");
            ObjectPositionValid valid(probe,bound.placement);
            auto check=[&](EntityPosition pos,std::uint8_t direction,std::int16_t flags){
                require(probe.declare_object_word(1,13,flags),"exact flag matrix input");
                return valid.evaluate({target,pos,direction,0,[&]{return current;}});
            };
            require(check({24,24,1,0},0,1).placement_error==21,"used required wall gives source unused-wall error21");
            input_owner.occupied_walls[0][5]=0;
            require(check({8,8,1,0},0,1).placement_error==5,"missing required wall gives source error5");
            require(check({24,24,1,0},0,256).placement_error==6,"forbidden wall side gives source error6");
            require(check({24,24,1,0},0,64).placement_error==22,"diagonal required on straight wall gives source error22");
            require(probe.declare_object_word(1,13,1),"restore actual required wall word");
            auto legacy=input;legacy.architecture_objects=nullptr;legacy.object_support=nullptr;legacy.position_registry=nullptr;
            const auto unknown=make_session_object_position_services(legacy);
            require(!unknown.placement.architecture(target,{24,24,1,0}),"legacy absence cannot invent unused walls");
            auto wrong=input;auto other=input_owner.object_support;wrong.object_support=&other;bool rejected=false;
            try{(void)make_session_object_position_services(wrong);}catch(const FormatError&){rejected=true;}
            require(rejected,"structurally equal copied support cannot masquerade as the same AOBJ owner");
            auto absence=*input_owner.position_registry;std::string why;
            require(!object_position_known_absent_oow(absence,*probe.find(1),why),"OOW by itself is not event-owned absence");
            require(record_object_footprint_absent(absence,probe,target,[]{return true;})&&
                record_object_moved_self(absence,probe,target,false,[]{return true;})&&
                object_position_known_absent_oow(absence,*probe.find(1),why),"real constructor and exact producer metadata establish known absence");
            auto stale=absence;++stale.motion.front().owner.generation;
            require(!object_position_known_absent_oow(stale,*probe.find(1),why),"stale generation cannot authorize nonspatial class change");
            auto referenced=absence;referenced.tiles.push_back({-1,{target}});
            require(!object_position_known_absent_oow(referenced,*probe.find(1),why),"any real tile collection reference defeats known absence");
            referenced=absence;referenced.rooms.front().entities.push_back(target);
            require(!object_position_known_absent_oow(referenced,*probe.find(1),why),"any RoomInfo collection reference defeats known absence");
            current=false;require(!bound.placement.architecture(target,{24,24,1,0}),"expired World/source lease rejects wall input");
        }
        state.object_state=objects;
        const auto source_bytes=encode_actorless_object_trade_state(state);
        CommunityObjectCommandServices services;services.source_still_matches=[]{return true;};
        std::optional<ObjectPositionIdentity> mutate_registered;
        services.bindings.extra_primitive_factory=[&](const ObjectTradeCandidateContext& context,const VmMemory&){
            return [&,world=&context.world](const VmFrame&,const Instruction& instruction,const Ts1Temps&,const VmDispatch&)->PrimitiveOutcome{
                if(instruction.opcode!=99)return {};
                if(mutate_registered){const auto* old=world->find(mutate_registered->id);
                    require(old&&old->generation==mutate_registered->generation&&old->position,"adversarial Init owns a real OLD registered source identity");
                    const auto word=old->object_words.find(40);require(word!=old->object_words.end(),"registered architectural source word exists");
                    require(world->declare_object_word(old->id,40,static_cast<std::int16_t>(std::uint16_t(word->second)^0x8000U)),
                        "actual VM-side mutation of registered architectural class");}
                return {PrimitiveDisposition::goto_true,{}};
            };
        };
        ObjectTradeSessionCommand command;command.kind=ObjectTradeSessionCommandKind::purchase;command.guid=guid;command.position={24,24,1,0};
        auto bought=prepare_community_object_command(resources,state,prepared,raw,rooms,command,services);
        require(bought.status==ObjectTradeTransactionStatus::prepared_success&&bought.candidate,
            "actual wall Init/VM/candidate purchase kind="+std::to_string(kind)+": "+bought.diagnostic);
        require(encode_actorless_object_trade_state(state)==source_bytes,"candidate wall writes never mutate source AOBJ/World");
        auto cold=decode_actorless_object_trade_state(bought.archive,&identity);
        require(encode_actorless_object_trade_state(cold)==bought.archive&&cold.object_state&&
            cold.object_state->occupied_walls[0][5]==2&&cold.object_state->occupied_walls[1][15]==8,
            "FIRST archive already captures actual occupied walls and unrelated history; cold reopen is byte-exact");
        require(cold.world.entities.size()==1&&cold.world.entities.front().object_words.at(6)==66&&
            cold.world.entities.front().object_words.at(43)==10,
            "wall EP6 executed and actual Init authored movement/delete bits2/8; price100 belongs only to OBJD");
        const auto id=cold.world.entities.front().id;const auto generation=cold.world.entities.front().generation;
        services.groups={{true,{{id,generation}}}};command.target={id,generation};
        if(kind){const auto& face=cold.object_state->wall_appearance.faces.front();
            require(face.owner==id&&face.generation==generation&&face.style.raw_style==(kind==1?3:22)&&face.door==(kind==1),
                "actual architectural Post captures concrete window/door style and ownership");}
        const auto stable=bought.archive;
        if(kind){
            mutate_registered=ObjectPositionIdentity{id,generation};auto attempt=command;attempt.position={40,24,1,0};
            const auto forged=prepare_community_object_command(resources,cold,prepared,raw,rooms,attempt,services);mutate_registered.reset();
            require(forged.status==ObjectTradeTransactionStatus::unsupported&&!forged.candidate&&
                forged.diagnostic.find("authority unavailable")!=std::string::npos&&encode_actorless_object_trade_state(cold)==stable,
                "registered architectural class mutation still invalidates the actual candidate; no source World/AOBJ publication");
        }
        auto rejected=command;rejected.position={40,40,1,0};rejected.kind=ObjectTradeSessionCommandKind::purchase;
        const auto invalid=prepare_community_object_command(resources,cold,prepared,raw,rooms,rejected,services);
        require(invalid.status!=ObjectTradeTransactionStatus::prepared_success&&encode_actorless_object_trade_state(cold)==stable,
            "no-wall target never publishes source mutations or fabricates wall support");
        command.kind=ObjectTradeSessionCommandKind::move;
        auto same=prepare_community_object_command(resources,cold,prepared,raw,rooms,command,services);
        require(same.status==ObjectTradeTransactionStatus::prepared_success&&same.candidate,
            "move to same wall excludes SELF by actual Pre clear, not heuristic subtraction: "+same.diagnostic);
        cold=decode_actorless_object_trade_state(same.archive,&identity);
        command.position={40,24,1,0};
        auto moved=prepare_community_object_command(resources,cold,prepared,raw,rooms,command,services);
        require(moved.status==ObjectTradeTransactionStatus::prepared_success&&moved.candidate,
            "actual wall move: "+moved.diagnostic);
        cold=decode_actorless_object_trade_state(moved.archive,&identity);
        require(cold.object_state->occupied_walls[0][5]==0&&cold.object_state->occupied_walls[0][6]==2,
            "source Pre clears old wall, Post sets new wall in same candidate AOBJ");
        if(kind)require(is_canonical_cleared_wall_face(cold.object_state->wall_appearance.faces.front()),
            "old wall keeps explicit style0 after move instead of resurrecting imported style");
        command.kind=ObjectTradeSessionCommandKind::sell;
        auto sold=prepare_community_object_command(resources,cold,prepared,raw,rooms,command,services);
        require(sold.status==ObjectTradeTransactionStatus::prepared_success&&sold.candidate,
            "actual wall delete kind="+std::to_string(kind)+" status="+std::to_string(static_cast<unsigned>(sold.status))+
            " decision="+std::to_string(static_cast<unsigned>(sold.decision.status))+
            " reason="+std::to_string(static_cast<unsigned>(sold.decision.reason))+": "+sold.diagnostic);
        const auto empty=decode_actorless_object_trade_state(sold.archive,&identity);
        require(empty.world.entities.empty()&&empty.object_state->occupied_walls[0][6]==0&&
            empty.object_state->occupied_walls[1][15]==8&&encode_actorless_object_trade_state(empty)==sold.archive,
            "delete clears occupancy, preserves unrelated history, and cold reload needs no retired object");
        for(const auto& face:empty.object_state->wall_appearance.faces)
            require(is_canonical_cleared_wall_face(face)&&!face.door&&!face.door_owner,
                "deleted door/window faces are strictly canonical clears");
        std::cout<<"wall transaction kind="<<kind<<" Init/place/self-move/move/delete/cold passed\n";
    }
}

void candidate_relationship_matrix(const std::filesystem::path& root){
    const auto game=root/"relationship-game";std::filesystem::create_directories(game);
    std::string objd(190,'\0');at(objd,0,138,4);at(objd,8,1,2);at(objd,18,4,2);
    at(objd,28,0x2345,4);at(objd,36,100,2);at(objd,44,1,2);at(objd,80,1,2);at(objd,116,2,2);at(objd,134,16,2);
    std::string objf(16+33*4,'\0');at(objf,4,1,4);objf.replace(8,4,"fJBO");at(objf,12,33,4);
    const auto object_file=game/"matrix.iff",global_file=game/"Global.far";
    write(object_file,iff({chunk("OBJD",128,objd),chunk("OBJf",128,objf)}));
    write(global_file,far({{"global.iff",iff({chunk("BHAV",256,assignments({{0,1}}))})}}));
    std::istringstream input(objd,std::ios::binary);
    DefinitionIndex index({{object_file.string(),"",128,read_object_definition(View(input,objd.size()))}});
    ObjectTradeResourceLibrary resources(game,index,global_file,"global.iff");
    const auto resource=resources.load_group(0x2345);
    PreparedArchitecture prepared;prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
    SavedArchitecture raw;raw.width=raw.height=4;raw.altitude=std::vector<std::array<std::uint8_t,4>>(16);
    for(unsigned level=0;level<2;++level){prepared.levels[level].floors.assign(16,1);prepared.levels[level].walls.resize(16);
        raw.levels[level].floors=prepared.levels[level].floors;raw.levels[level].walls.resize(16);}
    const auto rooms=generate_prepared_room_map(prepared);
    RelationshipSnapshot initial;initial.sources={{RelationshipMatrix::neighbor,41,0,{}},{RelationshipMatrix::neighbor,42,0,{}}};
    const auto initial_bytes=encode_relationship_snapshot(initial);
    const auto operation=[](unsigned opcode,unsigned mode,unsigned set,unsigned flags,unsigned field=2){
        if(opcode==24)return Instruction{24,254,255,{static_cast<std::uint8_t>(set),static_cast<std::uint8_t>(field),1,
            static_cast<std::uint8_t>(mode),static_cast<std::uint8_t>(flags),255,255,255}};
        return Instruction{26,254,255,{static_cast<std::uint8_t>(field),static_cast<std::uint8_t>(mode),
            static_cast<std::uint8_t>(flags|((set&1U)<<2U)|((set&2U)<<4U)),0,9,0,1,0}};
    };
    for(unsigned opcode:{24U,26U})for(unsigned kind=0;kind<3;++kind)for(unsigned mode=0;mode<4;++mode){
        World world;
        require(world.create(101,EntityKind::avatar,{0,88,0})&&world.create(202,EntityKind::avatar,{0,88,0})&&
            world.set_person_data(101,31,41)&&world.set_person_data(202,31,42)&&
            world.create(303,EntityKind::object,{2,0,0})&&world.define_type(303,0x2345)&&
            world.create(404,EntityKind::object,{2,0,0})&&world.define_type(404,0x2345),"candidate relationship actual participant banks");
        require(world.set_position(303,{16,16,1,0})&&world.set_position(404,{32,16,1,0}),"candidate spatial participants");
        SessionBundle components;components.world=world.snapshot();components.relationships=initial;
        RelationshipState isolated,reference;
        require(isolated.restore_snapshot(initial)&&reference.restore_snapshot(initial),"candidate and runtime-reference relationship snapshots restore");
        SessionHostState host;RandomState random(17);bool authority=true;unsigned extra_calls=0;
        ObjectTradeSessionHooks hooks;
        hooks.extra_primitive_factory=[&](const ObjectTradeCandidateContext&,const VmMemory&){return VmPrimitive{
            [&](const auto&,const auto&,const auto&,const auto&){++extra_calls;return PrimitiveOutcome{PrimitiveDisposition::unsupported,"unexpected extension primitive"};}};};
        auto factory=make_object_trade_candidate_factory(resources,components,prepared,raw,rooms,hooks,{},303,0x2345,0x2345,[&]{return authority;},nullptr,nullptr);
        auto binding=factory(world,isolated,host,random,nullptr,nullptr,resource);
        auto primitive=binding.primitive_factory(binding.memory);
        auto runtime=reference.primitive(world,binding.memory);
        const auto world_before=encode_world_snapshot(world.snapshot());
        const std::int16_t caller=kind==0?303:101,stack=kind==0?404:kind==1?202:42;
        const std::int16_t selected=kind==0?303:kind==1?101:41;
        VmFrame frame{routine_key(5000),0,{caller,caller,stack,0},{selected,55,0,0},{selected,0,0,0}};
        Ts1Temps temps{};const unsigned flags=kind==2?2U:0U;
        for(unsigned set:{1U,2U,0U}){
            frame.arguments[1]=static_cast<std::int16_t>(set==1?55:7);
            const auto ins=operation(opcode,mode,set,flags);
            const auto got=primitive(frame,ins,temps,{nullptr,17}),expected=runtime(frame,ins,temps,{nullptr,17});
            require(got.disposition==PrimitiveDisposition::goto_true&&got.disposition==expected.disposition,
                "candidate TS1/legacy direction and matrix dispatch matches existing runtime relationship service");
            require(encode_relationship_snapshot(isolated.snapshot())==encode_relationship_snapshot(reference.snapshot()),
                "candidate relationship creation/growth/set/increment/read preserves all runtime matrix bytes");
            if(set==0)require(got.write&&expected.write&&got.write->bank==VmFrameBank::arguments&&
                got.write->index==1&&got.write->value==62&&expected.write->value==62,"candidate read returns actual relation via its own frame output");
        }
        if(opcode==26){auto literal=operation(26,mode,1,flags);literal.operands[4]=7;literal.operands[6]=1;
            require(primitive(frame,literal,temps,{nullptr,17}).disposition==PrimitiveDisposition::goto_true&&
                runtime(frame,literal,temps,{nullptr,17}).disposition==PrimitiveDisposition::goto_true&&
                encode_relationship_snapshot(isolated.snapshot())==encode_relationship_snapshot(reference.snapshot()),
                "TS1 literal assignment follows the same dispatch shape as original PhoneGlobals8220");}
        require(extra_calls==0&&random.seed()==17&&encode_world_snapshot(world.snapshot())==world_before&&
            encode_relationship_snapshot(components.relationships)==initial_bytes,
            "relationship builtins never borrow extension/live owners or mutate source World/RNG/snapshot");
        const auto before=encode_relationship_snapshot(isolated.snapshot());
        require(primitive(frame,operation(opcode,mode,3,flags),temps,{nullptr,18}).disposition==PrimitiveDisposition::goto_true&&
            encode_relationship_snapshot(isolated.snapshot())==before,"set-mode3 preserves an existing relationship with no variable access");
        require(primitive(frame,operation(opcode,mode,1,flags|1U,5),temps,{nullptr,18}).disposition==PrimitiveDisposition::goto_false&&
            encode_relationship_snapshot(isolated.snapshot())==before,"no-create refuses growth without partial effects");
        if(kind==2){auto unknown=frame;unknown.context.stack_object=99;unknown.locals[0]=99;unknown.arguments[0]=99;
            require(primitive(unknown,operation(opcode,3,1,flags),temps,{nullptr,18}).disposition==PrimitiveDisposition::unsupported&&
                encode_relationship_snapshot(isolated.snapshot())==before,"unknown explicit neighbor source is never manufactured");}
        auto invalid=operation(opcode,4,1,flags);
        require(primitive(frame,invalid,temps,{nullptr,18}).disposition==PrimitiveDisposition::unsupported&&
            encode_relationship_snapshot(isolated.snapshot())==before,"unsupported direction refuses without partial relationship effects");
        invalid=operation(opcode,mode,1,flags);invalid.operands[opcode==24?2:6]=255;
        require(primitive(frame,invalid,temps,{nullptr,19}).disposition==PrimitiveDisposition::unsupported&&
            encode_relationship_snapshot(isolated.snapshot())==before,"invalid frame input rejects atomically in both formats");
        if(opcode==24&&kind==0&&mode==0){
            const Instruction distance{11,254,255,{0,0,1,9,0,0,0,0}};
            const Instruction direction{12,254,255,{1,0,8,0,1,9,0,0}};
            const auto d=primitive(frame,distance,temps,{nullptr,20});
            const auto a=primitive(frame,direction,temps,{nullptr,20});
            require(d.disposition==PrimitiveDisposition::goto_true&&d.temp_write&&d.temp_write->index==0&&d.temp_write->value==1&&
                a.disposition==PrimitiveDisposition::goto_true&&a.variable_write&&a.variable_write->scope==8&&
                a.variable_write->index==1&&a.variable_write->value==2,"candidate distance/direction use actual scoped participants and stage distinct VM outputs");
            auto missing=frame;missing.arguments[0]=999;
            require(primitive(missing,distance,temps,{nullptr,20}).disposition==PrimitiveDisposition::unsupported&&
                primitive(missing,direction,temps,{nullptr,20}).disposition==PrimitiveDisposition::unsupported,"spatial selectors cannot invent missing entities");
            auto bad_distance=distance;bad_distance.operands[0]=20;
            auto bad_direction=direction;bad_direction.operands[2]=255;
            require(primitive(frame,bad_distance,temps,{nullptr,20}).disposition==PrimitiveDisposition::unsupported&&
                primitive(frame,bad_direction,temps,{nullptr,20}).disposition==PrimitiveDisposition::unsupported,
                "spatial output bounds and unsupported setters remain fail closed");
            require(extra_calls==0&&random.seed()==17&&encode_world_snapshot(world.snapshot())==world_before&&
                encode_relationship_snapshot(isolated.snapshot())==before,"spatial queries only stage VM output without mutating candidate state");
            authority=false;
            require(primitive(frame,distance,temps,{nullptr,20}).disposition==PrimitiveDisposition::unsupported&&
                primitive(frame,direction,temps,{nullptr,20}).disposition==PrimitiveDisposition::unsupported,"expired authority also blocks both spatial services");
        }
        authority=false;
        require(primitive(frame,operation(opcode,mode,1,flags),temps,{nullptr,20}).disposition==PrimitiveDisposition::unsupported&&
            encode_relationship_snapshot(isolated.snapshot())==before&&extra_calls==0,"expired candidate authority rejects before relationship or extension dispatch");
    }
    // Synchronous services share the candidate World and VM output memory.
    // No route controller, renderer, live callback or artificial tick is lent.
    {
        World world;require(world.create(101,EntityKind::avatar,{0,80,0,16})&&
            world.create(303,EntityKind::object,{2,0,0})&&world.define_type(303,0x2345),"synchronous candidate participants");
        // Seed a normal pre-command avatar tick. SetMotiveChange must retain
        // its source ordering gate; the transaction itself never advances it.
        require(world.tick_motive_changes(),"existing session tick opens motive ordering gate before candidate creation");
        SessionBundle components;components.world=world.snapshot();
        const auto source_bytes=encode_world_snapshot(components.world);
        RelationshipState relations;SessionHostState host;host.global_values.push_back({31,4});
        RandomState random(17);bool authority=true;unsigned extra_calls=0;
        ObjectTradeSessionHooks hooks;
        hooks.extra_primitive_factory=[&](const ObjectTradeCandidateContext&,const VmMemory&){return VmPrimitive{
            [&](const auto&,const auto&,const auto&,const auto&){++extra_calls;return PrimitiveOutcome{PrimitiveDisposition::unsupported,"unowned service"};}};};
        auto factory=make_object_trade_candidate_factory(resources,components,prepared,raw,rooms,hooks,{},303,0x2345,0x2345,[&]{return authority;},nullptr,nullptr);
        auto binding=factory(world,relations,host,random,nullptr,nullptr,resource);
        auto primitive=binding.primitive_factory(binding.memory);
        VmFrame frame;frame.context={101,101,303,0};Ts1Temps temps{};temps[0]=19;
        Instruction look{22,254,255,{0,0,0,0,0,0,0,0}};
        require(primitive(frame,look,temps,{}).disposition==PrimitiveDisposition::goto_true&&
            world.find(101)->person_data[41]==303&&world.find(101)->person_data[42]==1&&
            world.find(101)->person_data[43]==0&&world.find(101)->person_data[44]==1&&world.find(101)->person_data[45]==0,
            "candidate head target publishes all five fields to its own avatar");
        const auto after_head=encode_world_snapshot(world.snapshot());look.operands[0]=1;
        require(primitive(frame,look,temps,{}).disposition==PrimitiveDisposition::goto_true&&
            encode_world_snapshot(world.snapshot())==after_head,"camera compatibility does not change head/body state");
        for(unsigned mode=2;mode<8;++mode){look.operands[0]=static_cast<std::uint8_t>(mode);
            require(primitive(frame,look,temps,{}).disposition==PrimitiveDisposition::unsupported&&
                primitive(frame,look,temps,{nullptr,0,true}).disposition==PrimitiveDisposition::unsupported&&
                encode_world_snapshot(world.snapshot())==after_head,"body modes never create a route or invent a synchronous check result");}
        const auto object_frame=[&]{auto f=frame;f.context.caller=303;return f;}();look.operands[0]=0;
        require(primitive(object_frame,look,temps,{}).disposition==PrimitiveDisposition::unsupported,
            "object caller cannot acquire avatar head state");
        Instruction motive{29,254,255,{7,7,0,2,5,0,40,0}};
        require(primitive(frame,motive,temps,{}).disposition==PrimitiveDisposition::goto_true&&world.find(101)->motives[0]==5,
            "one-shot motive update reaches candidate avatar");
        motive.operands[3]=0;motive.operands[4]=60;
        require(primitive(frame,motive,temps,{}).disposition==PrimitiveDisposition::goto_true&&
            world.find(101)->motives[0]==5&&world.find(101)->motive_changes[0].per_hour_change==60&&
            world.find(101)->motive_changes[0].maximum==40,"continuous motive installs a controller without ticking simulation");
        auto cold=decode_world_snapshot(encode_world_snapshot(world.snapshot()));World reopened;
        require(reopened.restore_snapshot(cold)&&reopened.find(101)->person_data[41]==303&&
            reopened.find(101)->motives[0]==5&&reopened.find(101)->motive_changes[0].per_hour_change==60,
            "cold codec preserves candidate head, motive and continuous rate state");
        const auto before_invalid=encode_world_snapshot(world.snapshot());
        auto invalid=motive;invalid.operands[2]=16;
        require(primitive(frame,invalid,temps,{}).disposition==PrimitiveDisposition::unsupported&&
            primitive(object_frame,motive,temps,{}).disposition==PrimitiveDisposition::unsupported&&
            encode_world_snapshot(world.snapshot())==before_invalid,"missing avatar/invalid motive refuses without effects");
        motive.operands[3]=1;
        require(primitive(frame,motive,temps,{}).disposition==PrimitiveDisposition::goto_true&&
            world.find(101)->motive_changes[0].per_hour_change==0,"clear resets the candidate continuous controller");
        for(unsigned mode=0;mode<256;++mode){Instruction generic{1,254,255,{static_cast<std::uint8_t>(mode),255,255,255,255,255,255,255}};
            const auto result=primitive(frame,generic,temps,{});
            if(generic_ts1_compatibility_mode(static_cast<std::uint8_t>(mode))){
                require(result.disposition==PrimitiveDisposition::goto_true,"finite compatibility profile is shared by candidate dispatch");
                if(mode==14){require(result.variable_write&&result.variable_write->scope==6&&result.variable_write->index==31&&
                    result.variable_write->value==19&&host.global_values[0].value==4,"radio mode stages a VM write without touching live globals");
                    require(binding.memory.write(frame,6,31,result.variable_write->value)&&host.global_values[0].value==19,
                        "effective candidate memory accepts the staged shared global write");host.global_values[0].value=4;}
            }else require(result.disposition==PrimitiveDisposition::unsupported,"unowned generic features are not blanket success");
        }
        const auto final=encode_world_snapshot(world.snapshot());authority=false;
        for(const auto ins:{look,motive,Instruction{1,254,255,{14,0,0,0,0,0,0,0}}})
            require(primitive(frame,ins,temps,{}).disposition==PrimitiveDisposition::unsupported,
                "expired candidate authority blocks every synchronous service before effects");
        require(encode_world_snapshot(world.snapshot())==final&&encode_world_snapshot(components.world)==source_bytes&&
            random.seed()==17&&host.global_values[0].value==4&&extra_calls>0,
            "services preserve source snapshots/RNG and reject services without an owner");
    }
    // A full native transaction, not merely the primitive table: effects from
    // Init0/EP8/EP11 survive publication and cold decoding; subsequent move/sell
    // and a failed later primitive use the same candidate service/abort owner.
    for(unsigned opcode:{24U,26U}){
        const auto file=game/("lifecycle-"+std::to_string(opcode)+".iff");const auto guid=0x3450U+opcode;
        auto def=objd;at(def,28,guid,4);
        auto entries=objf;
        for(const auto& e:std::vector<std::pair<unsigned,unsigned>>{{0,5000},{1,5001},{8,5002},{11,5003},{12,5004},{2,5005}})
            at(entries,16+e.first*4+2,e.second,2);
        const auto relation_code=[&](unsigned set,unsigned value,bool fail){
            auto b=assignments({{4,1},{13,128},{42,1},{23,2},{43,10}});
            b[5]=4;at(b,6,4,2);at(b,2,static_cast<std::uint32_t>(fail?12:11),2);b[12+4*12+2]=5;
            const auto add=[&](unsigned op,unsigned yes,std::array<std::uint8_t,8> args){put(b,op,2);b.push_back(static_cast<char>(yes));b.push_back(static_cast<char>(253));for(auto x:args)b.push_back(static_cast<char>(x));};
            add(2,6,{0,0,11,0,0,5,10,3}); // Actual own ID -> StackObject.
            add(2,7,{1,0,static_cast<std::uint8_t>(value),0,0,5,9,7});
            auto rel=operation(opcode,0,set,0);add(opcode,8,rel.operands);
            // Reading through the same opcode must return the value to Param1.
            auto read=operation(opcode,0,0,0);add(opcode,9,read.operands);
            add(2,10,{0,0,static_cast<std::uint8_t>(value),0,0,5,8,7}); // Literal -> actual VM Temp0.
            add(1,fail?11:254,{14,0,0,0,0,0,0,0}); // Shared global31 via VM variable_write.
            if(fail)add(655,254,{}); // Missing shared routine, AFTER matrix/global effects.
            return b;
        };
        const auto populate=[&](bool fail){write(file,iff({chunk("OBJD",128,def),chunk("OBJf",128,entries),
            chunk("BHAV",5000,relation_code(1,11,fail)),chunk("BHAV",5001,sleep_bhav()),
            chunk("BHAV",5002,relation_code(2,7,false)),chunk("BHAV",5003,relation_code(2,7,false)),
            chunk("BHAV",5004,relation_code(2,7,false)),chunk("BHAV",5005,relation_code(2,7,false))}));};
        populate(false);
        std::istringstream def_input(def,std::ios::binary);DefinitionIndex definitions({{file.string(),"",128,read_object_definition(View(def_input,def.size()))}});
        ObjectTradeResourceLibrary live_resources(game,definitions,global_file,"global.iff");
        SavedWorld original;original.world=std::make_unique<World>();original.relationships=std::make_unique<RelationshipState>();
        SessionResourceIdentity identity;identity.resource_fingerprint.fill(0x62);
        auto globals=RuntimeGlobals::from_legacy({}).snapshot();globals.words[32]=1;
        auto state=make_community_object_baseline(original,identity,globals,std::nullopt,17);
        const auto source_before=encode_actorless_object_trade_state(state);
        CommunityObjectCommandServices services;services.source_still_matches=[]{return true;};
        ObjectTradeSessionCommand cmd;cmd.kind=ObjectTradeSessionCommandKind::purchase;cmd.guid=guid;cmd.position={24,24,1,0};
        auto bought=prepare_community_object_command(live_resources,state,prepared,raw,rooms,cmd,services);
        require(bought.candidate&&bought.status==ObjectTradeTransactionStatus::prepared_success,
            "relationships in actual Init/EP8/EP11 transaction must prepare: "+bought.diagnostic);
        require(encode_actorless_object_trade_state(state)==source_before,"preparation cannot mutate the source actorless relationships or other state");
        auto reopened=decode_actorless_object_trade_state(bought.archive,&identity);
        require(encode_actorless_object_trade_state(reopened)==bought.archive&&reopened.relationships.pair_count==1&&
            reopened.relationships.value_count==3,"published candidate relationship pair/growth survives exact cold codec");
        require(reopened.globals.words[31]==7,
            "actual Init/placement VM stages, applies and captures Generic14 to candidate globals through cold codec");
        // VMEntity.Init runs EP0 then EP8. The constructor OOW post skips
        // dynamic update, but the actual in-world PostPositionChange runs EP8
        // again before the explicit user EP11: 11 + 7 + 7 + 7 = 32.
        const auto purchased_relation=reopened.relationships.sources.front().pairs.front().values.at(2);
        require(purchased_relation==32,"purchase relation opcode="+std::to_string(opcode)+
            " expected32 (Init11 + initial EP8 7 + placed Post EP8 7 + EP11 7), actual="+std::to_string(purchased_relation));
        const auto id=reopened.world.entities.front().id;const auto generation=reopened.world.entities.front().generation;
        cmd.kind=ObjectTradeSessionCommandKind::move;cmd.target={id,generation};cmd.position={40,24,1,0};
        const auto purchased_scene=prepare_community_object_scene(original,reopened,definitions,live_resources);
        services.groups=purchased_scene.physical_groups;
        const auto move_source=encode_actorless_object_trade_state(reopened);
        auto moved=prepare_community_object_command(live_resources,reopened,prepared,raw,rooms,cmd,services);
        require(moved.candidate&&moved.status==ObjectTradeTransactionStatus::prepared_success&&
            encode_actorless_object_trade_state(reopened)==move_source,"move lifecycle uses isolated relationship service: "+moved.diagnostic);
        auto moved_state=decode_actorless_object_trade_state(moved.archive,&identity);
        // Moving the singleton runs its in-world Post EP8 then user EP11.
        const auto moved_relation=moved_state.relationships.sources.front().pairs.front().values.at(2);
        require(moved_relation==46,"move relation opcode="+std::to_string(opcode)+
            " expected46 (purchase32 + moved Post EP8 7 + EP11 7), actual="+std::to_string(moved_relation));
        cmd.kind=ObjectTradeSessionCommandKind::sell;
        const auto moved_scene=prepare_community_object_scene(original,moved_state,definitions,live_resources);
        services.groups=moved_scene.physical_groups;
        const auto sell_source=encode_actorless_object_trade_state(moved_state);
        auto sold=prepare_community_object_command(live_resources,moved_state,prepared,raw,rooms,cmd,services);
        require(sold.candidate&&sold.status==ObjectTradeTransactionStatus::prepared_success&&
            encode_actorless_object_trade_state(moved_state)==sell_source,"sell pickup/retirement shares relationship service: "+sold.diagnostic);
        const auto sold_state=decode_actorless_object_trade_state(sold.archive,&identity);
        require(sold_state.world.entities.empty()&&sold_state.relationships.pair_count==0,"sell retires inbound/self relationship targets through the existing cleanup");
        populate(true);ObjectTradeResourceLibrary failing_resources(game,definitions,global_file,"global.iff");
        cmd.kind=ObjectTradeSessionCommandKind::purchase;cmd.target={};cmd.position={24,24,1,0};
        services.groups.clear(); // The abort case returns to the original empty source.
        const auto abort_before=encode_actorless_object_trade_state(state);
        auto failed=prepare_community_object_command(failing_resources,state,prepared,raw,rooms,cmd,services);
        require(!failed.candidate&&failed.status==ObjectTradeTransactionStatus::unsupported&&
            encode_actorless_object_trade_state(state)==abort_before,
            "failure after relationship writes discards candidate rather than publishing partial matrix effects");
    }
}
void lifecycle_and_journal(const std::filesystem::path& root){
    const auto game=root/"game";std::filesystem::create_directories(game/"GameData/Objects");std::filesystem::create_directories(game/"GameData/Global");
    std::string objd(190,'\0');at(objd,0,138,4);at(objd,8,1,2);at(objd,18,4,2);at(objd,28,0x1234,4);at(objd,36,100,2);at(objd,44,1,2);at(objd,80,1,2);at(objd,116,1,2);at(objd,134,16,2);
    std::string objf(16+33*4,'\0');at(objf,4,1,4);objf.replace(8,4,"fJBO");at(objf,12,33,4);
    for(const auto& p:std::vector<std::pair<unsigned,unsigned>>{{0,4096},{1,4097},{11,4098},{12,4099}})at(objf,16+p.first*4+2,p.second,2);
    const auto object_file=game/"GameData/Objects/chair.iff",global_file=game/"GameData/Global/Global.far";
    write(object_file,iff({chunk("OBJD",128,objd),chunk("OBJf",128,objf),
        chunk("BHAV",4096,init_with_refresh()),chunk("BHAV",4097,assignments({{0,77}})),
        chunk("BHAV",4098,assignments({{0,11}})),chunk("BHAV",4099,assignments({{0,12}}))}));
    write(global_file,far({{"global.iff",iff({chunk("BHAV",256,assignments({{0,1}})),chunk("BHAV",280,sleep_bhav())})}}));
    std::istringstream od(objd,std::ios::binary);const auto definition=read_object_definition(View(od,objd.size()));
    DefinitionIndex index({{object_file.string(),"",128,definition}});ObjectTradeResourceLibrary resources(game,index,global_file,"global.iff");
    const auto& record=resources.load(0x1234);require(record.catalog.state==ObjectTradeCatalogState::present&&record.catalog.price==100,"catalog comes from actual parsed OBJD, not a supplied quote");
    {
        const auto case_game=root/"case-game";std::filesystem::create_directories(case_game);
        const auto linked_file=case_game/"linked.iff",linked_global=case_game/"Global.far";
        write(linked_file,iff({chunk("OBJD",128,objd),chunk("OBJf",128,objf),
            chunk("GLOB",1,std::string(1,char(10))+"carglobals")}));
        const std::string constants{char(1),char(0),char(73),char(0)};
        write(linked_global,far({{"global.iff",iff({chunk("BHAV",256,assignments({{0,1}}))})},
            {"CarGlobals.IFF",iff({chunk("BHAV",8192,assignments({{0,2}})),chunk("BCON",8192,constants)})}}));
        DefinitionIndex linked_index({{linked_file.string(),"",128,definition}});
        ObjectTradeResourceLibrary linked_resources(case_game,linked_index,linked_global,"global.iff");
        const auto& linked=linked_resources.load(0x1234);
        require(linked.code.runtime.semiglobal_name==std::optional<std::string>{"carglobals"}&&
            linked.code.linked_routines.count(routine_key(8192))&&linked.code.linked_semiglobal&&
            linked.code.linked_semiglobal->name=="CarGlobals"&&linked.code.linked_semiglobal->entry=="CarGlobals.IFF"&&
            linked.code.linked_semiglobal->archive=="Global.far"&&
            linked.tuning.semiglobal_state==TuningSemiState::loaded&&
            linked.tuning.semiglobal_tables.at(8192).constants.at(0)==73,
            "trade library resolves logical GLOB case while retaining exact shared code/tuning/provenance");
    }
    const auto digest_before=sha256(reinterpret_cast<const std::uint8_t*>(objd.data()),objd.size());
    PreparedArchitecture prepared;prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
    SavedArchitecture raw;raw.width=raw.height=4;raw.altitude=std::vector<std::array<std::uint8_t,4>>(16);
    for(unsigned level=0;level<2;++level){prepared.levels[level].floors.assign(16,1);prepared.levels[level].walls.resize(16);raw.levels[level].floors=prepared.levels[level].floors;raw.levels[level].walls.resize(16);}
    const auto rooms=generate_prepared_room_map(prepared);
    // Original-import placement uses real saved bodies, registry events and
    // controller Thread authority. EP9 below is an explicit live Word write;
    // this fixture tests lifecycle transport, not native BHAV argument parity.
    // Exact decoded resource + physical bank parity through canonical provider.
    for(unsigned mode=0;mode<8;++mode){
        const auto version=mode==0?4U:mode==1?7U:10U;
        const auto span=version<6?34U:54U+(version-6U)*4U;
        std::string slot(16+span,'\0');at(slot,4,version,4);slot.replace(8,4,"TOLS");at(slot,12,1,4);
        at(slot,16,mode==5?3:0,2);
        const std::int32_t expected=mode==3?8:mode==4||mode==5||mode==6?-1:mode==7?1:5;
        if(version>=8)at(slot,74,mode==6?0xffffffffU:mode==7?1:mode==3?8:0,4);
        auto bound=objd;at(bound,40,1,2);
        const auto file=game/("slot-height-"+std::to_string(mode)+".iff");
        if(mode==4)write(file,iff({chunk("OBJD",128,bound),chunk("OBJf",128,objf)}));
        else write(file,iff({chunk("OBJD",128,bound),chunk("OBJf",128,objf),chunk("SLOT",1,slot)}));
        std::istringstream stream(bound,std::ios::binary);const auto def=read_object_definition(View(stream,bound.size()));
        DefinitionIndex defs({{file.string(),"",128,def}});ObjectTradeResourceLibrary lib(game,defs,global_file,"global.iff");
        const auto& loaded=lib.load(0x1234);const auto extent=loaded.code.runtime.construction.layout.containment_slots;
        World w;require(w.create(1,EntityKind::object,{0,0,extent})&&w.define_type(1,0x1234),"decoded height exact object bank");
        require(w.create(2,EntityKind::avatar,{0,88,0})&&w.create(3,EntityKind::object,{0,0,extent+1})&&w.define_type(3,0x1234)&&
            w.create(4,EntityKind::object)&&w.define_type(4,0x9999),"avatar, inconsistent-bank and unadmitted-resource controls");
        bool authorized=true;SessionObjectPositionContext ctx;ctx.world=&w;ctx.resources=&lib;ctx.prepared=&prepared;
        ctx.architecture=&raw;ctx.rooms=&rooms;ctx.source_still_matches=[&]{return authorized;};
        const auto services=make_session_object_position_services(ctx);
        const auto ref=[&](int id){return ObjectPositionIdentity{static_cast<std::int16_t>(id),w.find(id)->generation};};
        const auto source=ref(1);
        require(services.placement.slot0_height(source)==expected,"canonical source runtime height includes legacy/zero/known absence/signed values");
        require(services.placement.slot0_height(ref(2))==5,"avatar GetSlotHeight override does not require object resource admission");
        require(!services.placement.slot0_height(ref(3))&&!services.placement.slot0_height(ref(4)),"bank mismatch and unknown resource remain unsupported");
        authorized=false;require(!services.placement.slot0_height(source),"expired source cannot reuse decoded height");authorized=true;
        require(w.remove(1)&&w.create(1,EntityKind::object,{0,0,extent})&&w.define_type(1,0x1234),"height identity reuse control");
        require(!services.placement.slot0_height(source),"old height identity cannot bind replacement generation");
    }
    for(unsigned scenario=0;scenario<7;++scenario){
        World w;require(w.set_room_resolver([&](const auto& p){return geometric_room_at_runtime(rooms,p.x,p.y,p.level);}),
            "original avatar real geometric room resolver");
        std::vector<ObjectPositionIdentity> ids;
        for(std::int16_t id=1;id<=3;++id){
            std::array<std::int16_t,68> words{};words[3]=-1;words[8]=4;words[42]=1;
            require(w.create_saved_entity(id,id==1?EntityKind::avatar:EntityKind::object,0x1234,{0},
                id==1?std::vector<std::int16_t>(88):std::vector<std::int16_t>{},{},words,{},1),
                "original avatar fixture imports actual saved-body banks");
            std::vector<EntryPoint> entries(27);if(id==1)entries[9].action=4097;
            require(w.define_entry_points(id,entries)&&w.set_position(id,{id==2?40:24,id==2?40:24,1,
                static_cast<std::uint8_t>(id==3?3:0)}),"original avatar exact entry points and initial pose");
            ids.push_back({id,w.find(id)->generation});
        }
        const bool contained=scenario==1||scenario==3||scenario==4;
        const bool carrying=scenario==2;
        if(contained)require(w.attach(2,0,1)&&w.refresh_contained_position(1),"original person genuinely contained before recovery placement");
        if(carrying)require(w.attach(1,0,3)&&w.refresh_contained_position(3),"original person carries actual reciprocal child");
        require(w.set_radian_direction(1,0.13f),"original avatar preserves actual nonquantized angle");
        const auto original_radians=w.find(1)->radian_direction;
        const WorldEntityReference person{1,ids[0].generation};
        auto traversal=w.bind_original_load_order({{2,ids[1].generation},person,{3,ids[2].generation}},4);
        require(traversal.has_value(),"original avatar genuine complete source cohort token");
        Vm thread(resources.global_program(),ShareVmProgramTag{},w.vm_memory());
        require(thread.start(routine_key(256),{1,1,0})==VmStatus::running,"original person actual undispatched Main thread");
        const auto vm_before=thread.snapshot();
        ObjectPositionRegistryServices registry_services;registry_services.definitions=&index;
        registry_services.group=[](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{return std::vector<ObjectPositionIdentity>{id};};
        registry_services.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};
        registry_services.moved_self=[](auto)->std::optional<bool>{return false;};
        ObjectPositionRegistry initial(w,prepared,rooms,registry_services);
        for(const auto id:ids){const auto* e=w.find(id.id);const auto room=geometric_room_at_runtime(rooms,e->position->x,e->position->y,e->position->level);
            require(room&&initial.register_position(id,*room,true,[]{return true;}),"original avatar initial registry events");}
        require(initial.finish_initial_load(ids,[]{return true;}),"original avatar initial registry ownership complete");
        auto registry=initial.snapshot();bool authority=true;unsigned entry9=0,checks=0;
        SessionObjectPositionContext ctx;ctx.world=&w;ctx.resources=&resources;ctx.prepared=&prepared;
        ctx.architecture=&raw;ctx.rooms=&rooms;ctx.position_registry=&registry;ctx.memory=w.vm_memory();
        ctx.source_still_matches=[&]{return authority;};ctx.thread_for=[&](auto id){return id==1?&thread:nullptr;};
        for(const auto id:ids)ctx.groups.push_back({true,{{id.id,id.generation}}});
        ctx.native_entry_point9=[&](auto id){
            require(id.id==1&&id.generation==person.generation,"native placement EP9 exact person receiver");
            VmFrame frame;frame.context.caller=id.id;++entry9;
            return w.write(frame,3,25,static_cast<std::int16_t>(70+entry9));
        };
        std::vector<SessionPositionPhase> phases;std::vector<std::function<bool()>> retained;
        const auto old=w.find(1)->position,child_old=w.find(3)->position;
        ctx.position_phase=[&](auto id,const auto& point,auto phase,std::optional<std::uint8_t> direction,const std::function<bool()>& operation){
            require(id.id==person.id&&id.generation==person.generation,"placement phase authenticates exact imported person");
            require((phase==SessionPositionPhase::publish)==direction.has_value(),"only scalar phase receives direction authority");
            if(phase==SessionPositionPhase::post)require(w.find(1)->position==point,"Post receives actual root pose");
            if(phase==SessionPositionPhase::post&&carrying)require(w.find(3)->position==child_old,
                "scalar publication leaves carried child at old XYZ until recursive Post");
            phases.push_back(phase);retained.push_back(operation);return operation();
        };
        AvatarPositionValidServices geometry;
        geometry.architecture=[&](auto,const EntityPosition& p)->std::optional<AvatarPositionArchitecture>{
            ++checks;if(scenario==4||scenario==6)return {};
            AvatarPositionArchitecture out;out.out_of_bounds=p.x<0||p.y<0||p.x>=64||p.y>=64||p.level!=1;
            out.precise_floor=1;return out;
        };
        geometry.intersections=[&](auto id,const auto& p,auto){
            ObjectPositionRegistry current(w,prepared,rooms,registry_services,registry);
            const auto room=geometric_room_at_runtime(rooms,p.x,p.y,p.level);
            require(room.has_value(),"live candidate room before real registry collision query");
            return current.query(*room,avatar_candidate_collision_shape(p).box,id);
        };
        geometry.same_group=[](auto a,auto b)->std::optional<bool>{return a==b;};
        AvatarPositionValid validator(w,std::move(geometry));
        LiveOriginalRecoveryPlacementSource source{&w,&*traversal,person,&thread,thread.run_generation(),[&]{return authority;}};
        source.registration=w.begin_original_person_notifications(*traversal);
        if(scenario==5)traversal->close();
        const EntityPosition destination{scenario==3?-1:48,48,1,0};
        if(scenario==6){auto wrong=destination;wrong.direction=6;
            const auto refused=place_original_avatar_position(source,wrong,validator,ctx,false);
            require(refused.result.status==Ts1PlacementStatus::unsupported&&!refused.effects_started&&phases.empty()&&checks==0&&
                w.find(1)->position==old&&w.find(1)->radian_direction==original_radians,
                "native force refuses a fabricated heading before Pre or any publication");}
        const auto result=place_original_avatar_position(source,destination,validator,ctx,scenario!=6);
        if(scenario==5){
            require(result.result.status==Ts1PlacementStatus::unsupported&&!result.effects_started&&phases.empty()&&!checks&&
                registry.complete&&w.find(1)->position==old,"expired copied registration rejects before phases or geometry");
        }else if(scenario==4){
            require(result.result.status==Ts1PlacementStatus::unsupported&&result.effects_started&&!registry.complete&&
                phases==std::vector<SessionPositionPhase>{SessionPositionPhase::pre}&&!w.find(1)->attachment&&
                w.find(2)->slots[0]==0&&w.find(1)->position==old&&entry9==0,
                "failure after real Pre retains reciprocal container detachment and invalidates partial registry");
        }else{
            const bool blocked=scenario==3;
            require(result.result.status==(blocked?Ts1PlacementStatus::blocked:Ts1PlacementStatus::success)&&result.effects_started&&registry.complete,
                "original avatar lifecycle result scenario="+std::to_string(scenario)+": "+result.result.diagnostic);
            require(phases==(blocked?std::vector<SessionPositionPhase>{SessionPositionPhase::pre,SessionPositionPhase::post}:
                std::vector<SessionPositionPhase>{SessionPositionPhase::pre,SessionPositionPhase::publish,SessionPositionPhase::post})&&
                entry9==1&&w.find(1)->object_words.at(25)==71&&!w.find(1)->attachment,
                "real ordered lifecycle performs observable EP9 and leaves person on floor");
            require(w.find(1)->position==(blocked?old:std::optional<EntityPosition>{destination}),"blocked keeps old pose; accepted publishes exact destination");
            if(contained)require(w.find(2)->slots[0]==0,"contained person detachment clears actual reciprocal parent slot");
            if(carrying)require(w.find(1)->slots[0]==3&&w.find(3)->attachment&&w.find(3)->attachment->container==1&&
                w.find(3)->position->x==destination.x&&w.find(3)->position->y==destination.y&&w.find(3)->position->direction==3,
                "Post propagates carried child while preserving its independent heading");
            const auto room=geometric_room_at_runtime(rooms,w.find(1)->position->x,w.find(1)->position->y,1);
            require(room&&w.find(1)->object_words.at(29)==static_cast<std::int16_t>(*room-1),"avatar Room29 matches real published room");
            if(scenario==6)require(checks==0&&entry9==1&&registry.complete&&w.find(1)->position==std::optional<EntityPosition>{destination},
                "forced final placement bypasses unavailable geometry while retaining actual Pre/publish/Post, EP9 and registry ownership");
        }
        const auto phase_count=phases.size();const auto epoch=w.spatial_revision();
        for(const auto& callback:retained)require(!callback(),"retained phase callbacks are inert after placement returns");
        require(phases.size()==phase_count&&w.spatial_revision()==epoch&&thread.run_generation()==vm_before.run_generation&&
            thread.steps()==vm_before.steps&&thread.stack().front().pc==vm_before.stack.front().pc,
            "expired closures cannot mutate World and placement does not dispatch or restart original Main");
        require(w.find(1)->radian_direction==original_radians&&w.find(1)->object_words.at(1)==0,
            "native scalar publication never snaps the original person's continuous angle");
    }
    // Native4331d4 has different receivers/effects from generic Pre. These
    // fixtures use the real World + event-owned registry; callback markers test
    // ownership/order, without claiming original graphics-controller fidelity.
    for(unsigned scenario=0;scenario<21;++scenario){
        World w;require(w.set_room_resolver([&](const auto& p){return geometric_room_at_runtime(rooms,p.x,p.y,p.level);}),"native removal room owner");
        const bool object_root=scenario>=14;
        std::vector<ObjectPositionIdentity> ids;
        for(std::int16_t id=1;id<=3;++id){
            std::array<std::int16_t,68> words{};words[3]=-1;words[8]=0x104;words[14]=-1;words[42]=1;
            std::vector<EntryPoint> removal_entries(27);
            if(id==1&&(scenario==6||scenario==8))removal_entries[26].action=4097;
            require(w.create_saved_entity(id,id==1&&!object_root?EntityKind::avatar:EntityKind::object,0x1234,{0},
                id==1&&!object_root?std::vector<std::int16_t>(88):std::vector<std::int16_t>{},{},words,{},1)&&
                w.define_entry_points(id,removal_entries)&&w.set_position(id,{24,24,1,0}),"native removal real saved bodies");
            ids.push_back({id,w.find(id)->generation});
            require(w.publish_native_slot0({id,w.find(id)->generation},{}),"native removal known original zero-slot storage");
        }
        const bool contained=scenario==1||scenario==3||scenario==7||scenario==9||scenario==10||scenario==15||scenario==20;
        const bool carrying=scenario==2||scenario==3;
        const bool promoting=scenario>=18;
        const auto set=[&](std::int16_t id,std::int16_t n,std::int16_t value){VmFrame frame;frame.context.caller=id;return w.write(frame,3,n,value);};
        if(contained)require(w.attach(2,0,1)&&w.refresh_contained_position(1)&&set(1,26,2)&&set(1,14,1),"native removal exact parent/hierarchy");
        if(carrying)require(w.attach(1,0,3)&&w.refresh_contained_position(3)&&set(3,26,1)&&set(3,14,1),"native removal actual carried hierarchy");
        if(promoting)require(set(3,26,1)&&set(3,14,0)&&w.publish_native_slot0({1,ids[0].generation},{3,ids[2].generation}),"native zero child source reciprocity");
        if(scenario==19)require(set(1,26,2)&&set(1,14,0)&&w.publish_native_slot0({2,ids[1].generation},{1,ids[0].generation}),"native middle zero chain source reciprocity");
        if(!object_root)require(w.set_radian_direction(1,0.13f),"native removal nonquantized source angle");
        const auto angle=w.find(1)->radian_direction;const auto old=w.find(1)->position;
        const WorldEntityReference person{1,ids[0].generation};
        auto traversal=w.bind_original_load_order({{2,ids[1].generation},person,{3,ids[2].generation}},4,62);
        require(traversal.has_value(),"native removal authenticated original cohort");
        Vm thread(resources.global_program(),ShareVmProgramTag{},w.vm_memory());
        if(scenario!=17)require(thread.start(routine_key(256),{1,1,0})==VmStatus::running,"native removal actual Thread");
        else require(thread.run_generation()==0&&thread.steps()==0&&thread.stack().empty()&&
            thread.shared_program_for_owner(0)==resources.global_program(),"native object without Main has its installed real undispatched run0 Thread");
        ObjectPositionRegistryServices rs;rs.definitions=&index;
        rs.group=[](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{return std::vector<ObjectPositionIdentity>{id};};
        rs.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};rs.moved_self=[](auto)->std::optional<bool>{return false;};
        ObjectPositionRegistry initial(w,prepared,rooms,rs);
        for(const auto id:ids)require(initial.register_position(id,*geometric_room_at_runtime(rooms,24,24,1),true,[]{return true;}),"native removal initial spatial registrations");
        require(initial.finish_initial_load(ids,[]{return true;}),"native removal complete registry");
        auto registry=initial.snapshot();bool authority=true;std::vector<std::string> events;
        unsigned late_world_queries=0,expired_authority_queries=0;
        SessionObjectPositionContext ctx;ctx.world=&w;ctx.resources=&resources;ctx.prepared=&prepared;ctx.architecture=&raw;
        ctx.rooms=&rooms;ctx.position_registry=&registry;ctx.memory=w.vm_memory();ctx.source_still_matches=[&]{if(!authority)++expired_authority_queries;return authority;};
        ctx.thread_for=[&](auto id){if(!authority)++late_world_queries;return id==1?&thread:nullptr;};for(const auto id:ids)ctx.groups.push_back({true,{{id.id,id.generation}}});
        std::vector<std::function<bool()>> retained;
        ctx.position_phase=[&](auto id,const auto& destination,auto phase,const auto& heading,const std::function<bool()>& operation){
            require(!destination&&(phase==SessionPositionPhase::publish)==heading.has_value(),"native removal guard owns OOW without fabricated heading");
            const auto radians=w.find(1)->radian_direction;retained.push_back(operation);
            const bool done=operation();require(w.find(1)->radian_direction==radians,"native removal has no transient angle snap");
            require(id.id==1||carrying&&id.id==3,"native removal scalar guards cover actual recursive children");return done;
        };
        ctx.slot_phase=[&](auto child,auto parent,std::size_t slot,auto phase,const std::function<bool()>& operation){
            require(scenario==20&&child.id==3&&parent.id==2&&slot==0&&phase==SessionPositionPhase::publish,
                "native zero promotion uses real physical destination only when old slot was physical");
            retained.push_back(operation);return operation();
        };
        OriginalAvatarRemovalServices hooks;
        const bool active=scenario==8||scenario==11;bool dynamic_type6=false;unsigned admissions=0;
        if(scenario==8)require(set(1,16,1),"native tile-use condition derives from actual Light16");
        if(scenario==8)require(set(1,40,0x400),"native F2 condition derives from actual Flags2 bit400");
        hooks.conditions=[&](auto)->std::optional<OriginalAvatarRemovalConditions>{const auto* person_body=w.find(1);
            return OriginalAvatarRemovalConditions{active,person_body->object_words.at(16)==1,
                person_body->entry_points->at(26).action!=0,active||dynamic_type6,active,
                (std::uint16_t(person_body->object_words.at(40))&0x400U)!=0};};
        hooks.admit=[&](auto,std::string&){++admissions;return true;};
        hooks.entry=[&](auto id,std::uint16_t ep,std::string&){
            events.push_back(std::string("ep")+std::to_string(ep)+":"+std::to_string(id.id));
            require(ep==10?id.id==1:ep==9&&id.id==2,"native removal EP9 belongs only to the captured old parent");
            if(scenario==5){authority=false;return true;}
            if(scenario==16){require(thread.start(routine_key(256),{1,1,0})==VmStatus::running,"generic removal callback replaces actual Thread run");return true;}
            if(scenario==7&&ep==9)return false;
            if((scenario==9||scenario==10)&&ep==9){dynamic_type6=true;
                require(w.set_radian_direction(1,1.01f),"old-parent EP9 changes real heading before late branch");}
            return set(id.id,25,static_cast<std::int16_t>(ep));
        };
        hooks.move_bookkeeping=[&](auto top,auto person_id,std::string&){require(person_id.id==1,"native bounds invalidation receiver remains person");events.push_back("move");return set(top.id,17,71);};
        hooks.presentation_before=[&](auto,const auto&,std::string&){events.push_back("presentation");return scenario!=11||set(1,16,1);};
        hooks.tile_use=[&](auto,const auto&,std::string&){events.push_back("tile_use");return scenario!=11||set(1,40,0x400);};
        hooks.flagfield_event=[&](auto id,std::string&){events.push_back("F2");return set(id.id,18,242);};
        hooks.neighbor_updates=[&](auto,const auto&,std::string&){events.push_back("neighbors");return scenario!=6;};
        hooks.type6=[&](auto,const auto&,std::string&){events.push_back("type6");return true;};
        if(scenario==10)hooks.type6={};
        hooks.visual_controller=[&](auto,std::string&){events.push_back("controller");return true;};
        hooks.world_removed=[&](auto,std::string&){events.push_back("world");
            if(scenario==12||scenario==13){authority=false;
                // Retire the borrowed person while keeping this stack World
                // allocated. restore_snapshot intentionally refuses a populated
                // World, so use its real removal API to make the body absent.
                // An unauthorized resample would lose the earlier native pose.
                require(w.remove(1),"expired callback removes its borrowed person body");
                if(scenario==13)throw std::runtime_error("expired removal callback");
                return false;
            }return !w.find(1)->position&&!w.find(1)->attachment;};
        if(scenario==4){hooks.conditions=[](auto)->std::optional<OriginalAvatarRemovalConditions>{return OriginalAvatarRemovalConditions{true,false,false,false,false,false};};hooks.presentation_before={};}
        OriginalEntityRemovalAttempt result;
        if(object_root)result=remove_original_entity_from_world(person,ctx,hooks);
        else{
            LiveOriginalRecoveryPlacementSource source{&w,&*traversal,person,&thread,thread.run_generation(),[&]{return authority;}};
            source.source_still_matches=[&]{if(!authority)++expired_authority_queries;return authority;};
            source.registration=w.begin_original_person_notifications(*traversal);
            result=remove_original_avatar_from_world(source,ctx,hooks);
        }
        if(scenario==4)require(!result.completed&&!result.effects_started&&events.empty()&&registry.complete&&w.find(1)->position==old,
            "missing active presentation owner refuses before EP10 or physical effects");
        else if(scenario==5||scenario==16)require(!result.completed&&result.effects_started&&!registry.complete&&w.find(1)->position==old&&events.size()==1,
            "expired EP10 retains effects but prevents later bookkeeping and detach");
        else if(scenario==12||scenario==13)require(!result.completed&&result.effects_started&&!registry.complete&&
            result.native_pose==std::optional<EntityPosition>{{-16,-16,1,0}}&&expired_authority_queries>0&&late_world_queries==0&&!w.find(1),
            "expired/throwing callback preserves last authenticated OOW result without querying retired World/Thread owners");
        else{
            const bool failed=scenario==6||scenario==7||scenario==10;
            const auto expected_heading=static_cast<std::uint8_t>(w.find(1)->object_words.at(1));
            require(result.completed!=failed&&result.effects_started&&registry.complete!=failed&&result.native_pose==std::optional<EntityPosition>{{-16,-16,1,expected_heading}},
                "native removal result/partial registry matches completed physical effects: "+result.diagnostic);
            require(!w.find(1)->position&&!w.find(1)->attachment&&w.find(1)->unattached_slot==-1&&
                w.find(1)->object_words.at(26)==0&&w.find(1)->object_words.at(14)==-1&&
                (std::uint16_t(w.find(1)->object_words.at(8))&0x100U)==0&&w.find(1)->object_words.at(29)==-1,
                "native removal clears actual container/slot/native hierarchy, room and Flags8 bit100");
            require(w.find(1)->radian_direction==((scenario==9||scenario==10)?1.01f:angle)&&
                ((scenario==9||scenario==10)||w.find(1)->object_words.at(1)==0),"native removal preserves angle except the explicit old-parent EP9 writer");
            if(contained)require(w.find(2)->slots[0]==(promoting?3:0)&&result.old_parent==std::optional<WorldEntityReference>{{2,ids[1].generation}},"native removal captures old parent before unlinking reciprocal slot");
            if(promoting){
                require(w.native_slot0(person)==std::optional<WorldEntityReference>{{0,0}}&&w.find(3)->position==old,
                    "native zero-child promotion keeps child XYZ/heading and clears removed receiver backing");
                if(scenario==18)require(w.find(3)->object_words.at(14)==-1&&w.find(3)->object_words.at(26)==0&&!w.find(3)->attachment,
                    "native head removal promotes zero child to floor without fake attachment");
                if(scenario==19)require(w.native_slot0({2,ids[1].generation})==std::optional<WorldEntityReference>{{3,ids[2].generation}}&&
                    w.find(3)->object_words.at(14)==0&&w.find(3)->object_words.at(26)==2&&!w.find(3)->attachment&&!result.old_parent,
                    "native middle removal reconnects zero chain while logical old parent stays NULL");
                if(scenario==20)require(w.find(3)->attachment&&w.find(3)->attachment->container==2&&w.find(3)->attachment->slot==0&&
                    w.find(3)->object_words.at(14)==1&&w.find(3)->object_words.at(26)==2,
                    "native zero child replaces removed physical occupant using actual containment slot");
            }
            if(carrying)require(!w.find(3)->position&&w.find(3)->attachment&&w.find(3)->attachment->container==1&&w.find(1)->slots[0]==3&&
                w.find(3)->object_words.at(26)==1&&w.find(3)->object_words.at(14)==1,"native OOW scalar recurses while retaining carried child's actual hierarchy");
            for(const auto id:ids)if(id.id==1||contained&&id.id==2||carrying&&id.id==3)
                require(known_object_moved_self(registry,id)==std::optional<bool>{true},"top owner and contained cohort receive real MovedSelf true history");
            for(const auto& tile:registry.tiles)for(const auto id:tile.entities)require(id.id!=1&&(!carrying||id.id!=3),"removed subtree no longer belongs to any actual tile query");
            if(scenario==8)require(events==std::vector<std::string>{"ep10:1","move","presentation","tile_use","F2","neighbors","type6","controller","world"}&&w.find(1)->object_words.at(18)==242,"native conditional callbacks retain exact source order and real F2 bank effect");
            if(scenario==11)require(events==std::vector<std::string>{"ep10:1","move","presentation","tile_use","F2","type6","controller","world"}&&
                w.find(1)->object_words.at(16)==1&&w.find(1)->object_words.at(18)==242,
                "Light16 is reread after presentation and Flags2 is reread after tile-use mutation");
            if(scenario==9||scenario==10)require(dynamic_type6&&admissions==1&&events==
                (scenario==9?std::vector<std::string>{"ep10:1","move","ep9:2","type6","world"}:
                    std::vector<std::string>{"ep10:1","move","ep9:2"}),
                "late type6 condition is sampled after old-parent EP9, missing new owner preserves earlier effects, admission remains read-only and early");
            if(contained&&!failed)require(w.find(2)->object_words.at(25)==9&&w.find(1)->object_words.at(25)==10,"old parent EP9 cannot be silently replaced by person EP9");
            World restored;require(restored.restore_snapshot(w.snapshot()),"native removal physical World remains coherent for in-memory restore");
            if(scenario==17)require(thread.run_generation()==0&&thread.steps()==0&&thread.stack().empty()&&
                !w.find(1)->position&&!w.find(1)->attachment&&w.find(1)->unattached_slot==-1,
                "generic native removal completes actual World publication without manufacturing Main or incrementing run0");
        }
        const auto epoch=w.spatial_revision();for(const auto& callback:retained)require(!callback(),"native removal retained physical publishers are inert");
        require(epoch==w.spatial_revision(),"expired native removal publishers cannot alter World");
    }
    {World w;
        require(w.set_room_resolver([&](const auto& pos){return geometric_room_at_runtime(rooms,pos.x,pos.y,pos.level);}),"slot test actual room resolver");
        std::vector<ObjectPositionIdentity> ids;
        for(std::int16_t id=1;id<=3;++id){
            require(w.create(id,EntityKind::object,{0,0,1})&&w.define_type(id,0x1234)&&
                w.define_entry_points(id,std::vector<EntryPoint>(27)),"slot lifecycle typed source objects");
            for(std::int16_t n=0;n<=78;++n)if(n!=2&&n!=3&&n!=11&&n!=67)require(w.declare_object_word(id,n,n==8?4:0),"known ordinary slot object fields");
            require(w.set_position(id,{id==1?40:24,id==1?40:24,1,static_cast<std::uint8_t>(id==2?6:id==3?3:0)}),"slot original scalar poses");
            require(w.declare_object_word(id,1,id==2?6:id==3?3:0),"slot source own heading word");
            ids.push_back({id,w.find(id)->generation});
        }
        require(w.attach(2,0,3)&&w.refresh_contained_position(3),"actual nested item before source transfer");
        const auto child_old=w.find(3)->position;
        ObjectPositionRegistryServices registry_services;registry_services.definitions=&index;
        registry_services.group=[](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{return std::vector<ObjectPositionIdentity>{id};};
        registry_services.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};
        registry_services.moved_self=[](auto)->std::optional<bool>{return false;}; // Actual fresh constructors in this fixture.
        ObjectPositionRegistry initial(w,prepared,rooms,registry_services);
        for(const auto id:ids){const auto* e=w.find(id.id);const auto room=geometric_room_at_runtime(rooms,e->position->x,e->position->y,e->position->level);
            require(room&&initial.register_position(id,*room,true,[]{return true;}),"actual initial slot registry event");}
        require(initial.finish_initial_load(ids,[]{return true;}),"source load registry complete before transfer");
        auto registry=initial.snapshot();bool authority=true;std::vector<SessionSlotTransferPhase> phases;
        SessionObjectPositionContext ctx;ctx.world=&w;ctx.resources=&resources;ctx.prepared=&prepared;ctx.architecture=&raw;ctx.rooms=&rooms;
        ctx.position_registry=&registry;ctx.source_still_matches=[&]{return authority;};
        for(const auto id:ids)ctx.groups.push_back({true,{{id.id,id.generation}}});
        ctx.slot_phase=[&](auto item,auto container,std::size_t slot,auto phase,const std::function<bool()>& operation){
            require(item.id==2&&container.id==1&&slot==0&&authority,"slot phase receives exact source identities");phases.push_back(phase);
            if(phase==SessionSlotTransferPhase::post)require(w.find(2)->position->x==40&&w.find(3)->position==child_old,
                "only root moves before recursive Post; nested old position remains available for unregister");
            return operation();
        };
        auto services=make_session_object_position_services(ctx);
        const auto result=services.transfer_to_slot(ids[1],ids[0],0);
        require(result.completed&&result.placed,"shared ordinary slot transfer: "+result.diagnostic);
        require(phases==std::vector<SessionSlotTransferPhase>{SessionSlotTransferPhase::pre,SessionSlotTransferPhase::publish,SessionSlotTransferPhase::post}&&
            registry.complete&&w.find(2)->attachment->container==1&&w.find(2)->position->direction==6&&
            w.find(3)->attachment->container==2&&w.find(3)->position->x==40&&w.find(3)->position->direction==3,
            "Pre/scalar/Post preserves complete registry, reciprocal tree and independent headings");
        for(const auto id:ids)require(std::any_of(registry.rooms.begin(),registry.rooms.end(),[&](const auto& room){
            return std::any_of(room.entities.begin(),room.entities.end(),[&](auto member){return member.id==id.id&&member.generation==id.generation;});}),
            "every transferred entity remains owned by actual event registry");
        phases.clear();require(services.transfer_to_slot(ids[1],ids[0],0).placed&&phases.empty(),"source already-slot succeeds without extra Pre/Post");
        const auto occupied=services.transfer_to_slot(ids[2],ids[0],0);require(occupied.completed&&!occupied.placed&&phases.empty(),"occupied source slot returns false without effects");
        authority=false;require(!services.transfer_to_slot(ids[1],ids[0],0).completed&&registry.complete,"expired invocation cannot reuse an already-slot shortcut");
        authority=true;
        require(w.declare_object_word(2,42,1)&&w.declare_object_word(2,4,1)&&w.declare_object_word(2,23,2),"ordinary floor fixture source placement/rotation fields");
        std::optional<SessionPositionPhase> active_phase;
        std::function<bool()> retained_publisher;
        std::optional<EntityPosition> child_checkpoint;
        std::vector<SessionPositionPhase> floor_phases;
        bool unguarded=false,heading_at_old_xyz=false;
        ctx.slot_phase=[&](auto item,auto container,std::size_t slot,auto,const std::function<bool()>& operation){
            require(item.id==2&&container.id==1&&slot==0,"real scalar restoration/support identity");
            const auto previous=active_phase;active_phase=SessionPositionPhase::publish;
            const bool accepted=operation();active_phase=previous;return accepted;
        };
        ctx.position_phase=[&](auto id,const auto& desired,auto phase,std::optional<std::uint8_t> direction,const std::function<bool()>& operation){
            require(id.id==2&&id.generation==ids[1].generation,"ordinary factory scope pins actual identity");
            if(phase==SessionPositionPhase::publish){require(direction&&*direction<=7,"minted publication carries planned effective heading even for OOW");retained_publisher=operation;}
            else require(!direction,"Pre/Post carry no physical heading authority");
            if(phase==SessionPositionPhase::post)require(desired==w.find(2)->position,"Post scope uses actual root after restoration/support, not requested candidate");
            floor_phases.push_back(phase);active_phase=phase;
            const auto before=w.find(2)->position;const auto accepted=operation();
            if(phase==SessionPositionPhase::publish){
                require(w.find(3)->position==child_checkpoint,"factory scalar/heading scopes leave contained old XYZ for source Post unregister");
                if(before&&w.find(2)->position&&before->x==w.find(2)->position->x&&before->y==w.find(2)->position->y&&before->direction!=w.find(2)->position->direction)
                    heading_at_old_xyz=true;
            }
            active_phase.reset();return accepted;
        };
        const auto verify_index=[&]{
            require(registry.complete,"real native registry remains complete after ordinary placement");
            for(const auto id:ids){const auto* e=w.find(id.id);const auto offset=e->position?
                (e->position->level-1)*16+(e->position->y/16)*4+e->position->x/16:-1;
                std::size_t owners=0;for(const auto& tile:registry.tiles)for(const auto item:tile.entities)
                    if(item.id==id.id&&item.generation==id.generation){++owners;require(tile.offset==offset,"event-owned query uses actual current XYZ including carried/OOW");}
                require(owners==1,"every live source identity has exactly one current event-owned query entry");}
        };
        const auto move=[&](EntityPosition candidate,std::uint8_t direction,ObjectPositionChangeStatus expected){
            child_checkpoint=w.find(3)->position;floor_phases.clear();unguarded=false;
            auto bound=make_session_object_position_services(ctx);auto original_query=bound.placement.intersections;
            bound.placement.intersections=[&](auto id,const auto& p,auto heading){
                require(!active_phase,"actual PositionValid queries execute outside source Pre/publish/Post guards");return original_query(id,p,heading);};
            ObjectPositionValid placement(w,bound.placement);ObjectPositionChange movement(w,placement,bound.change);
            require(w.set_spatial_change_observer([&](World&,std::uint64_t){if(!active_phase)unguarded=true;}),"bind actual spatial observation for factory regression");
            const auto result=movement.apply_existing({ids[1],candidate,direction,0,[&]{return authority;}});
            require(w.set_spatial_change_observer({}),"release fixture spatial observer");
            require(result.status==expected,"ordinary shared lifecycle: "+result.diagnostic);
            require(!unguarded&&floor_phases.front()==SessionPositionPhase::pre&&floor_phases.back()==SessionPositionPhase::post,
                "all real ordinary position mutations are narrowly owned while source checking stays outside");
            verify_index();const auto revision=w.spatial_revision();
            if(retained_publisher)require(!retained_publisher()&&w.spatial_revision()==revision,"retained ordinary publisher expires without touching World/registry");
        };
        move({24,40,1,3},3,ObjectPositionChangeStatus::success);
        require(w.find(2)->position->direction==2&&w.find(2)->radian_direction==0&&w.find(3)->position->direction==3&&
            heading_at_old_xyz&&!w.find(2)->attachment,"held ordinary tree moves/quantizes heading without changing raw object radians or child heading");
        move({-32768,-32768,1,6},6,ObjectPositionChangeStatus::success);
        require(!w.find(2)->position&&!w.find(3)->position&&w.find(2)->object_words.at(1)==6,"OOW source publication retains explicit planned heading");
        move({24,24,1,4},4,ObjectPositionChangeStatus::success);
        auto rebound=make_session_object_position_services(ctx);
        const auto held=rebound.transfer_to_slot(ids[1],ids[0],0);require(held.completed&&held.placed,"restore held fixture through actual shared source kernel");
        require(w.declare_object_word(2,42,4096),"source check will reject ordinary floor but retain its effects");
        const auto old_pose=w.find(2)->position;
        move({24,40,1,2},2,ObjectPositionChangeStatus::blocked);
        require(w.find(2)->position==old_pose&&w.find(2)->attachment&&w.find(2)->attachment->container==1&&
            w.find(2)->object_words.at(42)==4096,"blocked ordinary placement restores actual old slot and preserves check-visible state");
        require(w.set_spatial_change_observer({}),"release prior fixture observer before actual Delete lease");
        ctx.position_phase=[&](auto id,const auto&,auto,std::optional<std::uint8_t>,const std::function<bool()>& operation){
            const auto* child=w.find(id.id);return child&&child->generation==id.generation&&operation();
        };
        auto deletion=make_session_object_position_services(ctx);
        for(unsigned pass=0;pass<2;++pass)require(deletion.before_slot_transfer(ids[0],true),"two actual parent source Pre calls before removed-container lease");
        AvatarPositionValid unused_avatar_validator(w,AvatarPositionValidServices{});
        HouseholdPlacementBindings lifecycle_bindings{w,prepared,rooms,unused_avatar_validator};
        lifecycle_bindings.lifecycle_object_context=[&](const Ts1LifecyclePlacementInvocation& invocation){
            auto context=ctx;context.source_still_matches=invocation.source_still_matches;
            context.retired_container=invocation.container;
            context.retired_parent_moved_self=invocation.parent_moved_self;
            context.detach_container=[&,lease=invocation.container](auto child,auto parent,std::size_t slot){
                const auto exact=lease.parent();return parent.id==exact.id&&parent.generation==exact.generation&&
                    w.detach_retained_slot(lease,slot,{child.id,child.generation});
            };return context;
        };
        HouseholdPlacementAdapter lifecycle_adapter(std::move(lifecycle_bindings));
        const auto parent_moved_self=known_object_moved_self(registry,ids[0]);
        const auto child_anchor=w.find(2)->position;
        const auto child_heading=child_anchor->direction;
        const auto parent_heading=w.find(1)->position->direction;
        std::optional<Ts1LifecyclePlacementInvocation> expired;
        require(w.remove_retaining_contents({ids[0].id,ids[0].generation},[&](const World::RetiredContainer& lease){
            require(!w.find(1)&&w.resolve_reference(lease.parent())&&w.find(2)->attachment&&
                w.find(2)->attachment->container==1,"source removal frees numeric parent while child links retain exact old body");
            // The real retirement owner releases constructor/footprint metadata
            // only after both source Pre events removed its actual memberships.
            registry.motion.erase(std::remove_if(registry.motion.begin(),registry.motion.end(),[&](const auto& value){return value.owner.id==1&&value.owner.generation==ids[0].generation;}),registry.motion.end());
            registry.footprints.erase(std::remove_if(registry.footprints.begin(),registry.footprints.end(),[&](const auto& value){return value.owner.id==1&&value.owner.generation==ids[0].generation;}),registry.footprints.end());
            Ts1LifecyclePlacementInvocation invocation{lease,[&w,lease]{return w.retired_container_active(lease);},parent_moved_self};
            expired=invocation;
            require(w.copy_retired_position(lease,{ids[1].id,ids[1].generation}),"retained parent anchor copies raw actual child Position");
            const auto moved=lifecycle_adapter.place_lifecycle(invocation,{ids[1].id,ids[1].generation},{},child_heading,0);
            require(moved.status==Ts1PlacementStatus::success,"actual household floor kernel drops retained-parent child OOW without VM/frame/opcode fabrication: "+moved.diagnostic);
            require(!w.find(2)->attachment&&!w.find(2)->position&&!w.find(3)->position&&
                w.find(2)->object_words.at(1)==child_heading&&w.find(3)->object_words.at(1)==3,
                "source Pre detaches old body then OOW Post carries subtree while preserving independent headings");
            const auto placed=lifecycle_adapter.find_lifecycle(invocation,{ids[1].id,ids[1].generation});
            require(placed.status==Ts1PlacementStatus::blocked,"real source radial search retains legitimate OOW child when all candidates reject: "+placed.diagnostic);
            return registry.complete;
        }),"actual resource/geometry/registry adapter completes scoped removed-container drop");
        const auto* retained_parent=w.resolve_reference({ids[0].id,ids[0].generation});
        require(retained_parent&&retained_parent->position&&child_anchor&&
            retained_parent->position->x==child_anchor->x&&retained_parent->position->y==child_anchor->y&&
            retained_parent->position->level==child_anchor->level&&retained_parent->position->direction==parent_heading&&registry.complete,
            "retained parent copies child raw XYZ/level while preserving its own heading and complete source index");
        require(expired&&lifecycle_adapter.place_lifecycle(*expired,{ids[1].id,ids[1].generation},EntityPosition{24,24,1,0},0).status==Ts1PlacementStatus::unsupported,
            "expired native source capability cannot be reused as placement authority");

    }
    {World support_world;require(support_world.create(2,EntityKind::object)&&support_world.define_type(2,0x1234)&&
        support_world.declare_object_word(2,59,8)&&support_world.set_position(2,{24,24,1,0}),"actual shared support event source");
        auto support=make_architecture_object_support(4,4);bool authorized=true;
        SessionObjectPositionContext context;context.world=&support_world;context.resources=&resources;
        context.prepared=&prepared;context.architecture=&raw;context.rooms=&rooms;context.object_support=&support;
        context.source_still_matches=[&]{return authorized;};
        auto services=make_session_object_position_services(context);const auto* entity=support_world.find(2);
        const ObjectPositionIdentity id{2,entity->generation};const auto pose=*entity->position;
        require(services.change.object_supported&&services.change.object_supported(id,pose,true)&&support.levels[0][5]==1,
            "shared lifecycle callback publishes actual source event into owning support grid");
        require(services.change.object_supported(id,pose,false)&&support.levels[0][5]==0,"shared preposition callback clears support");
        authorized=false;require(!services.change.object_supported(id,pose,true)&&support.levels[0][5]==0,"shared callback preserves real invocation authority");authorized=true;
        context.object_support=nullptr;auto legacy=make_session_object_position_services(context);
        require(!legacy.change.object_supported(id,pose,true)&&support.levels[0][5]==0,"unknown legacy support cannot initialize as empty");
        auto foreign=make_architecture_object_support(8,8);context.object_support=&foreign;auto wrong=make_session_object_position_services(context);
        require(!wrong.change.object_supported(id,pose,true)&&foreign.levels[0][9]==0,"support from another geometry domain cannot publish");}
    {World refresh_world;require(refresh_world.create(1,EntityKind::object)&&refresh_world.declare_object_word(1,7,120)&&
        refresh_world.set_position(1,{24,24,1,0}),"real Refresh contribution source");
        const auto refresh=session_refresh_primitive(refresh_world,&rooms);VmFrame frame;frame.context.caller=1;
        Instruction instruction{7,254,255,{0,0,2,0,0,0,0,0}};
        const auto score=calculate_room_score(rooms,room_score_contributions(refresh_world),24,24,1);require(score.has_value(),"shared room score proof");
        require(refresh(frame,instruction,{},{}).disposition==PrimitiveDisposition::goto_true,"area Refresh uses complete actual geometry and contributions");
        instruction.operands[2]=1;require(refresh(frame,instruction,{},{}).disposition==PrimitiveDisposition::unsupported,"lighting cannot be replaced with successful cache invalidation");
        instruction.operands[2]=0;require(refresh(frame,instruction,{},{}).disposition==PrimitiveDisposition::goto_true,"graphics is derived from live World on every renderer capture");
        instruction.operands[2]=2;instruction.operands[0]=1;frame.context.stack_object=99;
        require(refresh(frame,instruction,{},{}).disposition==PrimitiveDisposition::unsupported,"area Refresh cannot invent a missing target room");}

    std::array<std::uint8_t,32> identity{};identity[0]=0x72;Family family;family.family_number=12;family.budget=9000;family.member_guids={123456};std::vector<FamilyEntry> families;families.push_back({21,std::move(family)});
    const auto managed=plan_family_managed_move_in_overlay(families,identity,nullptr,21,7,1000);
    FamilySessionJournal pending(families,identity,std::nullopt,7,21,managed);pending.commit_move_in();
    FamilySessionJournal journal(families,identity,managed,7,21);auto live=source();live.family_lease=journal.lease();
    live.architecture_object_support=make_architecture_object_support(4,4);
    live.architecture_object_support->levels[0][15]=1;live.architecture_object_support->levels[1][3]=1;
    const auto source_support=live.architecture_object_support;
    unsigned validations=0;bool fail_validation=false,mutate_capture=false,use_actual_source_admissions=false,forge_avatar_policy=false,omit_cross_owner=false;std::map<std::int16_t,VmSavedRoutinePolicy> actual_admissions;unsigned captures=0;
    ObjectTradeSessionHooks hooks;
    bool job_provider_available=true;unsigned init_job_reads=0;
    hooks.job_data=[&](const VmFrame& frame,std::uint16_t job,std::int16_t level,std::int16_t field)->std::optional<std::int16_t>{
        ++init_job_reads;
        require(frame.key.id==4096&&frame.context.code_owner==0x1234&&frame.context.caller>1&&
            frame.context.caller==frame.context.callee&&job==65535&&level==2&&field==12,
            "purchased Init supplies its exact candidate Thread career selectors and resource context");
        return job_provider_available?std::optional<std::int16_t>{8}:std::nullopt;
    };
    hooks.capture_current=[&]{++captures;auto copy=live;if(mutate_capture&&captures%2==0)++*copy.host.family_budget;return copy;};
    hooks.validate_reopen=[&](const SessionBundle& candidate,const std::vector<std::uint8_t>& bytes){++validations;require(encode_session_bundle(candidate)==bytes&&encode_session_bundle(decode_session_bundle(bytes))==bytes,"candidate full native codec validation before publication");if(fail_validation)throw FormatError("injected native restore rejection");};
    hooks.physical_groups=[&]{std::vector<ObjectTradeGroup> groups;for(const auto& e:live.world.entities)if(e.kind==EntityKind::object)groups.push_back({true,{{e.id,e.generation}}});return groups;};
    auto empty=std::make_shared<const VmProgram>(RoutineSet{});
    hooks.thread_programs=[&](std::int16_t id,const SessionBundle& s){ObjectTradeThreadPrograms p;p.globals=resources.global_program();
        if(id==1){p.primary_owner=123456;p.owners.emplace(123456,empty);if(forge_avatar_policy)p.saved_routine_policy=VmSavedRoutinePolicy::defer_missing_or_pc_fault;}else{const auto t=std::find_if(s.object_threads.begin(),s.object_threads.end(),[&](const auto& x){return x.id==id;});require(t!=s.object_threads.end(),"thread provenance exists");p.primary_owner=t->primary_owner;p.owners.emplace(p.primary_owner,std::make_shared<const VmProgram>(record.code.runtime.private_routines));
            for(const auto& bank:t->resource_owners)if(bank.owner!=p.primary_owner&&!omit_cross_owner){
                auto program=std::make_shared<const VmProgram>(record.code.runtime.private_routines);
                require(session_loaded_bank_fingerprint(program->routines())==bank.routine_fingerprint,
                    "fixture authenticates each exact extra installed bank from its captured resource manifest");
                p.owners.emplace(bank.owner,std::move(program));
            }
            if(use_actual_source_admissions){const auto admitted=actual_admissions.find(id);if(admitted!=actual_admissions.end())p.saved_routine_policy=admitted->second;}}return p;};
    ObjectTradeSessionAdapter adapter(resources,journal,prepared,raw,rooms,hooks);
    ObjectTradeSessionCommand command;const auto unchanged=encode_session_bundle(live);
    auto result=adapter.execute(command);require(result.state==ObjectTradeSessionState::cancelled&&captures==0&&!journal.checkpoint_path(),"cancel performs no capture/allocation/publication");
    command.kind=ObjectTradeSessionCommandKind::purchase;command.guid=0x1234;command.position={24,24,1,2};
    job_provider_available=false;result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::unsupported&&!journal.checkpoint_path()&&encode_session_bundle(live)==unchanged,
        "unavailable candidate JobData rejects purchase before charging, checkpoint publication or live mutation");
    job_provider_available=true;
    fail_validation=true;result=adapter.execute(command);require(result.state==ObjectTradeSessionState::unsupported&&!journal.checkpoint_path()&&encode_session_bundle(live)==unchanged,"technical reopen failure leaves authority, budget and World unchanged");fail_validation=false;
    captures=0;mutate_capture=true;result=adapter.execute(command);require(result.state==ObjectTradeSessionState::unsupported&&!journal.checkpoint_path(),"changed live source cannot publish candidate");mutate_capture=false;
    result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"real purchase adapter: "+result.diagnostic);
    require(validations>=3&&result.archive==encode_session_archive(read_session_archive(result.checkpoint))&&encode_session_bundle(live)==unchanged,"actual journal bytes, no in-place live World publication");
    journal.validate_restore(result.archive);live=decode_session_bundle(result.archive);
    require(live.architecture_object_support==source_support,"purchase roundtrip preserves exact event-owned support, rather than rebuilding occupancy");
    require(*live.host.family_budget==7900&&live.object_trade&&live.object_trade->version==2&&live.object_trade->objects.empty()&&live.object_trade->groups.size()==1&&live.created_objects.size()==1&&live.object_threads.size()==1,"reopened purchase persists funds, identity, ledger and actual object Thread");
    const auto id=live.created_objects.front().id;auto entity=std::find_if(live.world.entities.begin(),live.world.entities.end(),[&](const auto& e){return e.id==id;});
    require(live.object_trade->groups.front().members.front().disabled==0,
        "purchase persists proven TS1 runtime Disabled0 independently of catalog and saved flags");
    require(entity!=live.world.entities.end()&&entity->position==std::optional<EntityPosition>{{24,24,1,2}}&&entity->object_words.at(0)==11,"actual Init/EP11 and placement execute before published checkpoint");
    require(init_job_reads>=4&&entity->attributes.size()==1&&entity->attributes[0]==8,
        "real purchase Init JobData result survives placement, immutable archive and fresh decoder");
    require(live.object_threads.front().vm.steps==0&&live.object_threads.front().vm.stack.size()==1&&live.object_threads.front().vm.stack.front().key.id==4097,"Main staged but not executed inside command");
    World reopened;require(reopened.restore_snapshot(live.world),"fresh World restore");Vm main(resources.global_program(),ShareVmProgramTag{},reopened.vm_memory(),{}, {},SharedRoutineOwners{{0x1234,std::make_shared<const VmProgram>(record.code.runtime.private_routines)}},ShareVmOwnerProgramsTag{});
    require(main.restore_snapshot(live.object_threads.front().vm)&&main.run(8)==VmStatus::returned_true&&reopened.find(id)->object_words.at(0)==77,"fresh restored Main actually executes native source tree");
    {
        auto probe=command;probe.kind=ObjectTradeSessionCommandKind::sell;probe.target={id,entity->generation};probe.move_objects=false;
        const auto before_probe=encode_session_bundle(live);const auto prior_checkpoint=journal.checkpoint_path();
        const auto before_validations=validations;fail_validation=true;
        const auto enabled=adapter.execute(probe);fail_validation=false;
        require(enabled.state==ObjectTradeSessionState::unsupported&&validations>before_validations&&
            journal.checkpoint_path()==prior_checkpoint&&encode_session_bundle(live)==before_probe,
            "known Disabled0 passes sale permission through candidate reopen, without publishing the probe: state="+
            std::to_string(static_cast<int>(enabled.state))+" transaction="+std::to_string(static_cast<int>(enabled.transaction))+
            " validations="+std::to_string(validations-before_validations)+" diagnostic="+enabled.diagnostic);
        auto& disabled=live.object_trade->groups.front().members.front().disabled;disabled=std::uint8_t{1};
        const auto disabled_source=encode_session_bundle(live);const auto before_rejection=validations;
        const auto rejected=adapter.execute(probe);
        require(rejected.state==ObjectTradeSessionState::rejected&&validations==before_rejection&&
            journal.checkpoint_path()==prior_checkpoint&&encode_session_bundle(live)==disabled_source,
            "known runtime Disabled1 overrides fresh-created fallback and rejects before reopen/publication: state="+
            std::to_string(static_cast<int>(rejected.state))+" transaction="+std::to_string(static_cast<int>(rejected.transaction))+
            " diagnostic="+rejected.diagnostic);
        disabled=std::uint8_t{0};
    }
    command.kind=ObjectTradeSessionCommandKind::move;command.target={id,entity->generation};command.position={40,40,1,6};command.move_objects=true;
    result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"real move adapter: "+result.diagnostic);live=decode_session_bundle(result.archive);
    require(live.architecture_object_support==source_support,"move/rotation preserves source support grid in candidate and new archive");
    entity=std::find_if(live.world.entities.begin(),live.world.entities.end(),[&](const auto& e){return e.id==id;});
    require(*live.host.family_budget==7900&&entity->position==std::optional<EntityPosition>{{40,40,1,6}}&&live.object_trade->groups.front().initial_price==100,"move/rotation persist physical pose without debit or ledger repricing");
    command.kind=ObjectTradeSessionCommandKind::sell;result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"real sale adapter: "+result.diagnostic);live=decode_session_bundle(result.archive);
    require(live.architecture_object_support==source_support,"sale preserves unrelated support history");
    require(*live.host.family_budget==8000&&live.created_objects.empty()&&live.object_threads.empty()&&live.object_trade->objects.empty()&&live.object_trade->groups.empty()&&std::none_of(live.world.entities.begin(),live.world.entities.end(),[&](const auto& e){return e.id==id;}),"real sale journal retires source identity/Main and refunds exact initial price");
    require(live.object_history->retired_created_objects.size()==1&&live.object_history->reserved_entity_ids.size()==1,"retired object cannot reuse source identity");
    require(digest_before==sha256(reinterpret_cast<const std::uint8_t*>(objd.data()),objd.size()),"input definition buffer is read-only");
    // A real imported dormant object can retain an invalid saved Main PC.
    // Preserve the proven source import policy only for that unaffected Thread;
    // never weaken command/Init execution or an avatar's VM restoration.
    command.kind=ObjectTradeSessionCommandKind::purchase;command.guid=0x1234;command.position={24,24,1,0};
    result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"dormant import fixture purchase: "+result.diagnostic);
    live=decode_session_bundle(result.archive);auto& dormant=live.object_threads.front();dormant.vm.status=VmStatus::dormant;dormant.vm.stack.front().pc=999;
    for(auto& scheduled:live.scheduler.entities)if(scheduled.id==dormant.id)scheduled.status=VmStatus::dormant;
    Vm strict_vm(resources.global_program(),ShareVmProgramTag{},VmMemory{}, {}, {},SharedRoutineOwners{{0x1234,std::make_shared<const VmProgram>(record.code.runtime.private_routines)}},ShareVmOwnerProgramsTag{});
    require(!strict_vm.restore_snapshot(dormant.vm)&&strict_vm.restore_snapshot(dormant.vm,VmSavedRoutinePolicy::defer_missing_or_pc_fault),"actual native source import policy admits saved dormant PC while default remains strict");
    const auto dormant_id=dormant.id;actual_admissions.emplace(dormant_id,VmSavedRoutinePolicy::defer_missing_or_pc_fault);
    const auto dormant_bytes=encode_vm_snapshot(dormant.vm);const auto prior_head=journal.checkpoint_path();
    command.position={56,56,1,0};result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::unsupported&&journal.checkpoint_path()==prior_head&&result.diagnostic.find("entity="+std::to_string(dormant_id))!=std::string::npos&&result.diagnostic.find("pc_out_of_range")!=std::string::npos,
        "unproven/default policy refuses import with exact entity/frame reason and no publication");
    use_actual_source_admissions=true;result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"authenticated dormant import continuation: "+result.diagnostic);
    live=decode_session_bundle(result.archive);const auto retained=std::find_if(live.object_threads.begin(),live.object_threads.end(),[&](const auto& t){return t.id==dormant_id;});
    require(retained!=live.object_threads.end()&&encode_vm_snapshot(retained->vm)==dormant_bytes&&live.object_threads.size()==2&&*live.host.family_budget==7800,
        "unaffected dormant Thread survives a real purchase/checkpoint exactly while new object uses strict Main staging");
    require(strict_vm.activate_dormant_state()&&strict_vm.run(1)==VmStatus::fault,"actual admitted object transitions to inert fault without changing its load policy");
    const auto fault=std::find_if(live.object_threads.begin(),live.object_threads.end(),[&](const auto& t){return t.id==dormant_id;});
    fault->vm=strict_vm.snapshot();for(auto& scheduled:live.scheduler.entities)if(scheduled.id==dormant_id){scheduled.status=fault->vm.status;scheduled.steps=fault->vm.steps;}
    const auto fault_bytes=encode_vm_snapshot(fault->vm);command.position={24,56,1,0};
    use_actual_source_admissions=false;const auto fault_head=journal.checkpoint_path();result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::unsupported&&journal.checkpoint_path()==fault_head&&result.diagnostic.find("pc_out_of_range")!=std::string::npos,
        "mere fault status cannot authorize a relaxed object restoration");
    use_actual_source_admissions=true;result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"retained exact load admission after source fault: "+result.diagnostic);
    live=decode_session_bundle(result.archive);const auto inert=std::find_if(live.object_threads.begin(),live.object_threads.end(),[&](const auto& t){return t.id==dormant_id;});
    require(inert!=live.object_threads.end()&&encode_vm_snapshot(inert->vm)==fault_bytes&&*live.host.family_budget==7700,
        "authenticated import policy survives fault transition and a fresh real checkpoint without executing or repairing inert Thread");
    World waiting_world;require(waiting_world.restore_snapshot(live.world),"restore World for actual deferred sleeping child");
    Sleep sleeper(waiting_world,[]{});Vm sleeping(resources.global_program(),ShareVmProgramTag{},waiting_world.vm_memory(),{},sleeper.primitive(),
        SharedRoutineOwners{{0x1234,std::make_shared<const VmProgram>(record.code.runtime.private_routines)}},ShareVmOwnerProgramsTag{});
    VmSnapshot imported;imported.status=VmStatus::dormant;imported.run_generation=1;
    const VmContext imported_context{dormant_id,dormant_id,0,0x1234};
    imported.stack.push_back({routine_key(4109),0,imported_context,{0,0,0,0},std::vector<std::int16_t>(4)});
    imported.stack.push_back({routine_key(280),0,imported_context,{5,0,0,0},std::vector<std::int16_t>(4)});
    require(!sleeping.restore_snapshot(imported)&&sleeping.restore_snapshot(imported,VmSavedRoutinePolicy::defer_missing_or_pc_fault)&&
        sleeper.bind(sleeping)&&sleeper.join_at(live.host.tick)&&sleeping.activate_dormant_state()&&sleeping.run(1)==VmStatus::waiting,
        "source admission carries a missing parent while real global Sleep child executes and parks");
    auto waiting=std::find_if(live.object_threads.begin(),live.object_threads.end(),[&](const auto& t){return t.id==dormant_id;});
    waiting->vm=sleeping.snapshot();waiting->sleep=sleeper.snapshot();const auto waiting_vm=encode_vm_snapshot(waiting->vm);const auto waiting_sleep=waiting->sleep;
    for(auto& scheduled:live.scheduler.entities)if(scheduled.id==dormant_id){scheduled.status=waiting->vm.status;scheduled.steps=waiting->vm.steps;scheduled.run_generation=waiting->vm.run_generation;}
    (void)encode_session_bundle(live); // Establish a valid source controller/clock baseline before testing restore policy.
    command.position={56,24,1,0};use_actual_source_admissions=false;const auto waiting_head=journal.checkpoint_path();result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::unsupported&&journal.checkpoint_path()==waiting_head&&result.diagnostic.find("routine_missing")!=std::string::npos,
        "waiting status alone never authorizes missing-parent restoration: state="+std::to_string(static_cast<unsigned>(result.state))+" diagnostic="+result.diagnostic);
    use_actual_source_admissions=true;result=adapter.execute(command);require(result.state==ObjectTradeSessionState::published_reload_required,"exact source admission retained while waiting: "+result.diagnostic);
    live=decode_session_bundle(result.archive);const auto parked=std::find_if(live.object_threads.begin(),live.object_threads.end(),[&](const auto& t){return t.id==dormant_id;});
    require(parked!=live.object_threads.end()&&encode_vm_snapshot(parked->vm)==waiting_vm&&parked->sleep.job&&
        parked->sleep.job->ticket==waiting_sleep.job->ticket&&parked->sleep.job->deadline==waiting_sleep.job->deadline&&
        parked->vm.pending->instruction.instruction.opcode==0&&*live.host.family_budget==7600,
        "fresh checkpoint preserves actual waiting child/parent, exact sleep ownership and finance effects without status heuristics");
    forge_avatar_policy=true;const auto protected_head=journal.checkpoint_path();result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::unsupported&&result.diagnostic.find("authenticated imported object")!=std::string::npos&&journal.checkpoint_path()==protected_head,
        "a provider cannot apply deferred object import policy to a resident avatar");forge_avatar_policy=false;
    // The selected Thread needs the same authenticated import admission as
    // unaffected Threads. A waiting status alone does not authorize it.
    const auto selected_import=std::find_if(live.world.entities.begin(),live.world.entities.end(),[&](const auto& e){return e.id==dormant_id;});
    require(selected_import!=live.world.entities.end(),"selected imported object remains live");
    auto selected_thread=std::find_if(live.object_threads.begin(),live.object_threads.end(),[&](const auto& t){return t.id==dormant_id;});
    require(selected_thread!=live.object_threads.end(),"selected imported Thread exists for cross-owner continuation");
    const CodeOwner foreign_owner=0x80005678U;
    auto foreign_manifest=selected_thread->resource_owners.front();foreign_manifest.owner=foreign_owner;
    foreign_manifest.identity=SessionCodeOwnerIdentity::process;
    selected_thread->resource_owners.push_back(foreign_manifest);live.next_code_owner=foreign_owner+1;
    for(auto& frame:selected_thread->vm.stack)frame.context.code_owner=foreign_owner;
    if(selected_thread->vm.pending){selected_thread->vm.pending->context.code_owner=foreign_owner;
        selected_thread->vm.pending->instruction.code_owner=foreign_owner;}
    selected_thread->vm.temps[7]=234;selected_thread->vm.temp_xl[0]=123456;
    const auto selected_vm=encode_vm_snapshot(selected_thread->vm);
    const auto selected_sleep=selected_thread->sleep;
    (void)encode_session_bundle(live);
    require(selected_thread->resource_owners.front().owner<foreign_owner&&live.next_code_owner>foreign_owner&&
        foreign_manifest.resource_guid==selected_thread->guid,"adapter cross-owner manifest/allocator preserve exact resource identity");
    const auto selected_programs=hooks.thread_programs(dormant_id,live);
    World verified_source_world;require(verified_source_world.restore_snapshot(live.world),"cross-owner adapter source World");
    Sleep verified_source_sleep(verified_source_world);
    Vm verified_source(selected_programs.globals,ShareVmProgramTag{},verified_source_world.vm_memory(),{},
        verified_source_sleep.primitive(),selected_programs.owners,ShareVmOwnerProgramsTag{});
    require(verified_source.restore_snapshot(selected_thread->vm,selected_programs.saved_routine_policy)&&
        verified_source_sleep.bind(verified_source)&&verified_source_sleep.restore_snapshot(selected_sleep)&&
        encode_vm_snapshot(verified_source.snapshot())==selected_vm,
        "actual admitted cross-owner VM/Sleep source restores before selected command matrix");
    command.kind=ObjectTradeSessionCommandKind::move;command.target={dormant_id,selected_import->generation};
    command.position={24,56,1,6};command.move_objects=true;
    use_actual_source_admissions=false;const auto selected_head=journal.checkpoint_path();result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::unsupported&&journal.checkpoint_path()==selected_head,
        "selected imported Thread still refuses without its exact admission");
    use_actual_source_admissions=true;const auto selected_budget=*live.host.family_budget;
    omit_cross_owner=true;result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::unsupported&&journal.checkpoint_path()==selected_head,
        "selected Thread cannot omit an installed foreign owner even with its actual source restore policy");
    omit_cross_owner=false;
    result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::published_reload_required,"selected waiting import move uses actual source admission: "+result.diagnostic);
    live=decode_session_bundle(result.archive);
    const auto moved_import=std::find_if(live.object_threads.begin(),live.object_threads.end(),[&](const auto& t){return t.id==dormant_id;});
    require(moved_import!=live.object_threads.end()&&encode_vm_snapshot(moved_import->vm)==selected_vm&&
        moved_import->sleep.job&&selected_sleep.job&&moved_import->sleep.job->ticket==selected_sleep.job->ticket&&
        moved_import->sleep.job->deadline==selected_sleep.job->deadline&&*live.host.family_budget==selected_budget,
        "selected move preserves exact foreign-owner/missing-parent frames, Temp/XL and actual Sleep ticket/deadline");
    command.kind=ObjectTradeSessionCommandKind::sell;use_actual_source_admissions=false;
    const auto selected_sale_head=journal.checkpoint_path();result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::unsupported&&journal.checkpoint_path()==selected_sale_head,
        "selected sale cannot infer import admission from previous checkpoint");
    use_actual_source_admissions=true;result=adapter.execute(command);
    require(result.state==ObjectTradeSessionState::published_reload_required,"selected waiting import sale uses actual source admission: "+result.diagnostic);
    live=decode_session_bundle(result.archive);
    require(*live.host.family_budget==selected_budget+100&&std::none_of(live.world.entities.begin(),live.world.entities.end(),[&](const auto& e){return e.id==dormant_id;})&&
        std::none_of(live.object_threads.begin(),live.object_threads.end(),[&](const auto& t){return t.id==dormant_id;}),
        "selected deferred Main/Sleep retires through real sale and refunds only its registered price");
    // This second journal owns an actual complete RoomInfo ledger and a
    // lighting owner. The previous cases intentionally preserve legacy absence.
    {
        auto full=source();full.world.entities.front().kill_timeout=0;
        World initial_world;require(initial_world.restore_snapshot(full.world),"full registry source World");
        require(initial_world.define_entry_points(1,std::vector<EntryPoint>(27)),"full registry avatar entry table");
        for(std::int16_t n=0;n<=78;++n)if(n!=2&&n!=3&&n!=11&&n!=67)
            require(initial_world.declare_object_word(1,n,0),"full registry avatar source words");
        // Ordinary source controllers can legitimately remain unregistered after
        // original recovery Pre resets an old OOW placement and skips Post.
        // Build that state through the real event producer, retaining its owned
        // present footprint and MovedSelf=true rather than fabricating a ledger.
        std::vector<ObjectPositionIdentity> absent;
        for(std::int16_t id:{2,3}){
            require(initial_world.create_runtime_object(id,0x1234,{0,0,0},std::vector<EntryPoint>(27))&&
                initial_world.set_position(id,{8,8,1,0}),"ordinary source controller and real initial placement");
            absent.push_back({id,initial_world.find(id)->generation});
        }
        ObjectPositionRegistryServices rs;rs.definitions=&index;
        rs.routing_slot_count=[](auto)->std::optional<std::size_t>{return 0;};
        rs.moved_self=[](auto)->std::optional<bool>{return false;};
        rs.group=[](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{return std::vector<ObjectPositionIdentity>{id};};
        ObjectPositionRegistry registry(initial_world,prepared,rooms,rs);
        const ObjectPositionIdentity avatar{1,initial_world.find(1)->generation};
        require(registry.register_position(avatar,0,true,[]{return true;}),"real avatar registry event");
        std::vector<ObjectPositionIdentity> initial_cohort{avatar};
        for(auto id:absent){
            require(registry.register_position(id,*geometric_room_at_runtime(rooms,8,8,1),true,[]{return true;}),"real source controller registry event");
            initial_cohort.push_back(id);
        }
        require(registry.finish_initial_load(initial_cohort,[]{return true;}),"real source registry complete by actual load events");
        for(auto id:absent){
            require(registry.record_moved_self(id,true,[]{return true;})&&
                registry.unregister_position(id,true,[]{return true;})&&
                publish_source_individual_position(initial_world,{id.id,id.generation},{},0,[]{return true;}),
                "source controller real Pre/unregister and OOW publication without Post");
            std::string reason;
            require(object_position_known_absent_oow(registry.snapshot(),*initial_world.find(id.id),reason),
                "producer-owned known-absent OOW: "+reason);
            const auto& bindings=registry.snapshot().footprints;
            const auto bound=std::find_if(bindings.begin(),bindings.end(),[&](const auto& x){return x.owner.id==id.id;});
            require(bound!=bindings.end()&&bound->present&&!bound->room&&
                known_object_moved_self(registry.snapshot(),id)==std::optional<bool>{true},
                "actual OOW fixture retains present/unbound footprint and true MovedSelf");
        }
        const auto absent_registry=registry.snapshot();
        ArchitectureObjectState state;state.object_support=make_architecture_object_support(4,4);
        for(auto& walls:state.occupied_walls)walls.assign(16,0);
        state.position_registry=registry.snapshot();state.lighting.emplace();state.lighting_committed.emplace();
        full.architecture_object_state=state;full.world=initial_world.snapshot();
        auto full_identity=identity;full_identity[0]=0x89;
        auto full_plan=plan_family_managed_move_in_overlay(families,full_identity,nullptr,21,7,1000);
        FamilySessionJournal full_pending(families,full_identity,std::nullopt,7,21,full_plan);full_pending.commit_move_in();
        FamilySessionJournal full_journal(families,full_identity,full_plan,7,21);full.family_lease=full_journal.lease();
        auto full_hooks=hooks;full_hooks.capture_current=[&]{return full;};
        full_hooks.validate_reopen=[&](const SessionBundle& candidate,const std::vector<std::uint8_t>& bytes){
            hooks.validate_reopen(candidate,bytes);
            require(candidate.architecture_object_state&&candidate.architecture_object_state->lighting,
                "final transaction validation receives its actual lighting owner");
            for(const auto& entity:candidate.world.entities)if(!entity.dead&&!entity.ui_ghost)
                require(room_lighting_cached_visual_room(*candidate.architecture_object_state->lighting,{entity.id,entity.generation}).has_value(),
                    "first validated adapter archive retains kernel-captured current visual cohort");
        };
        full_hooks.physical_groups=[&]{std::vector<ObjectTradeGroup> groups;for(const auto& e:full.world.entities)
            if(e.kind==EntityKind::object)groups.push_back({true,{{e.id,e.generation}}});return groups;};
        World* candidate_world=nullptr;bool inject_init_pose=false,inject_init_word=false;
        full_hooks.extra_primitive_factory=[&](const ObjectTradeCandidateContext& ctx,const VmMemory&){candidate_world=&ctx.world;return VmPrimitive{};};
        full_hooks.job_data=[&](const VmFrame&,auto,auto,auto)->std::optional<std::int16_t>{
            require(candidate_world,"actual candidate World supplied by factory");
            if(inject_init_pose)require(candidate_world->set_position(1,{8,8,1,0}),"adversarial Init changes unrelated real pose");
            if(inject_init_word)require(candidate_world->declare_object_word(1,6,42),"nonspatial Init word notification");
            return 8;
        };
        ObjectTradeSessionAdapter full_adapter(resources,full_journal,prepared,raw,rooms,full_hooks);
        ObjectTradeSessionCommand c;c.kind=ObjectTradeSessionCommandKind::purchase;c.guid=0x1234;c.position={24,24,1,2};
        const auto original_full=encode_session_bundle(full);
        inject_init_pose=true;auto rejected=full_adapter.execute(c);inject_init_pose=false;
        require(rejected.state==ObjectTradeSessionState::unsupported&&!full_journal.checkpoint_path()&&
            encode_session_bundle(full)==original_full,"unowned Init pose invalidates candidate and never publishes/changes original");
        inject_init_word=true;auto bought=full_adapter.execute(c);inject_init_word=false;
        require(bought.state==ObjectTradeSessionState::published_reload_required,"full ledger purchase: "+bought.diagnostic);
        full=decode_session_bundle(bought.archive);
        const auto selected=full.created_objects.back();
        require(full.object_trade&&full.object_trade->version==2&&full.object_trade->objects.empty()&&
            full.object_trade->groups.size()==1&&full.object_trade->groups.front().initial_price==100,
            "full lighting capture retains the transaction-owned shared value ledger");
        const auto verify=[&]{
            const auto& r=*full.architecture_object_state->position_registry;
            require(r.complete&&full.architecture_object_state->lighting&&full.architecture_object_state->lighting_committed,
                "archive retains complete actual registry and both lighting owners");
            for(const auto& e:full.world.entities){unsigned tile_count=0,room_count=0;
                const auto offset=e.position?(e.position->level-1)*16+e.position->y/16*4+e.position->x/16:-1;
                for(const auto& tile:r.tiles)for(auto id:tile.entities)if(id.id==e.id&&id.generation==e.generation){
                    ++tile_count;require(tile.offset==offset,"published event tile agrees with actual current pose");}
                for(const auto& room:r.rooms)for(auto id:room.entities)if(id.id==e.id&&id.generation==e.generation)++room_count;
                const auto old=std::find_if(absent.begin(),absent.end(),[&](auto id){return id.id==e.id;});
                if(old==absent.end())require(tile_count==1&&room_count==1,"registered live identity retains exact tile/room");
                else {
                    std::string reason;
                    require(tile_count==0&&room_count==0&&object_position_known_absent_oow(r,e,reason),
                        "transaction preserves actual known-absent source controller: "+reason);
                    const auto binding_for=[&](const auto& state){return std::find_if(state.footprints.begin(),state.footprints.end(),[&](const auto& b){return b.owner.id==e.id;});};
                    const auto prior=binding_for(absent_registry),now=binding_for(r);
                    require(prior!=absent_registry.footprints.end()&&now!=r.footprints.end()&&
                        prior->owner.generation==now->owner.generation&&prior->present==now->present&&
                        prior->instance==now->instance&&prior->box==now->box&&prior->room==now->room&&
                        known_object_moved_self(r,*old)==std::optional<bool>{true},
                        "buy/move/sell preserve exact unbound footprint and MovedSelf metadata");
                }
                require(room_lighting_cached_visual_room(*full.architecture_object_state->lighting,{e.id,e.generation}).has_value(),
                    "first committed transaction archive already classifies every live identity before fresh-process first frame");
            }
            World reopened_world;require(reopened_world.restore_snapshot(full.world),"lighting consumer restores exact candidate World");
            auto actual=*full.architecture_object_state;
            const auto before_first_frame=encode_architecture_object_state(actual);
            SessionRoomLighting controller(reopened_world,[&]{return &actual;},[&]{return &rooms;},[]{return static_cast<ObjectPositionRegistry*>(nullptr);},[&]{
                RoomLightingServices services;services.definitions=&index;
                services.group=[](auto id)->std::optional<std::vector<ObjectPositionIdentity>>{return std::vector<ObjectPositionIdentity>{id};};
                services.object_state=[](auto)->std::optional<RoomLightingObjectState>{return RoomLightingObjectState{};};return services;
            });
            for(const auto& e:full.world.entities)require(controller.visual_room(e.id,e.generation)==
                room_lighting_visual_room(reopened_world,{e.id,e.generation},0),
                "first frame after restore reads exact authored room class before any tick or lighting drain");
            require(encode_architecture_object_state(actual)==before_first_frame,
                "complete candidate visual cache restores without changing committed/deferred/event state");
            controller.process();
            require(actual.position_registry->complete,"actual restored lighting safe point accepts complete registry");
        };
        verify();
        const auto successful_head=full_journal.checkpoint_path();const auto successful_bytes=bought.archive;
        inject_init_pose=true;auto failed_second=full_adapter.execute(c);inject_init_pose=false;
        require(failed_second.state==ObjectTradeSessionState::unsupported&&full_journal.checkpoint_path()==successful_head&&
            encode_session_archive(read_session_archive(*successful_head))==successful_bytes,
            "failed candidate preserves existing journal HEAD and immutable checkpoint bytes");
        c.kind=ObjectTradeSessionCommandKind::move;c.target={selected.id,selected.generation};c.position={40,40,1,6};c.move_objects=true;
        auto moved_full=full_adapter.execute(c);require(moved_full.state==ObjectTradeSessionState::published_reload_required,"full ledger move: "+moved_full.diagnostic);
        full=decode_session_bundle(moved_full.archive);verify();
        const auto moved_entity=std::find_if(full.world.entities.begin(),full.world.entities.end(),[&](const auto& e){return e.id==selected.id;});
        require(moved_entity!=full.world.entities.end()&&moved_entity->position==std::optional<EntityPosition>{{40,40,1,6}}&&
            moved_entity->object_words.at(1)==6,"full registry move restores temporary check heading then publishes exact planned heading/pose");
        c.kind=ObjectTradeSessionCommandKind::sell;
        auto sold_full=full_adapter.execute(c);require(sold_full.state==ObjectTradeSessionState::published_reload_required,"full ledger sale: "+sold_full.diagnostic);
        full=decode_session_bundle(sold_full.archive);verify();
        require(full.created_objects.empty()&&full.architecture_object_state->position_registry->footprints.size()==1+absent.size(),
            "sale removes only the retired constructor metadata after real unregister events");
        const auto& sold_registry=*full.architecture_object_state->position_registry;
        require(!sold_registry.deferred_lighting.empty()&&std::none_of(sold_registry.rooms.begin(),sold_registry.rooms.end(),[&](const auto& room){
            return std::any_of(room.entities.begin(),room.entities.end(),[&](auto id){return id.id==selected.id&&id.generation==selected.generation;});
        }),"first validated sale archive retains real deferred lighting receipts and no retired RoomInfo owner");
        require(full.object_trade&&full.object_trade->version==2&&full.object_trade->groups.empty(),
            "full lighting sale preserves actual value-group retirement instead of restoring captured source prices");
        const auto valid_full=full;
        for(unsigned scenario=0;scenario<5;++scenario){
            full=valid_full;
            auto& actual=*full.architecture_object_state->position_registry;
            auto body=std::find_if(full.world.entities.begin(),full.world.entities.end(),[&](const auto& e){return e.id==absent.front().id;});
            auto motion=std::find_if(actual.motion.begin(),actual.motion.end(),[&](const auto& x){return x.owner.id==body->id;});
            auto bound=std::find_if(actual.footprints.begin(),actual.footprints.end(),[&](const auto& x){return x.owner.id==body->id;});
            switch(scenario){
            case 0:body->native_out_of_world_level.reset();break;
            case 1:body->position=EntityPosition{8,8,1,0};body->native_out_of_world_level.reset();break;
            case 2:actual.motion.erase(motion);break;
            case 3:++motion->owner.generation;break;
            case 4:bound->room=0;break;
            }
            const auto prior_head=full_journal.checkpoint_path();
            std::string absent_reason;
            require(!object_position_known_absent_oow(actual,*body,absent_reason),
                "read-only absent proof rejects malicious state, scenario "+std::to_string(scenario));
            // A stale metadata owner is rejected by the session codec itself.
            // Component codecs retain the deliberately hostile cross-component
            // pairing so we can also prove the adapter rejects it unchanged.
            if(scenario==3){
                bool stale_codec_rejected=false;
                try{(void)encode_session_bundle(full);}catch(const FormatError&){stale_codec_rejected=true;}
                require(stale_codec_rejected,"session codec independently rejects stale metadata owner");
            }
            const auto prior_world=encode_world_snapshot(full.world);
            const auto prior_architecture=encode_architecture_object_state(*full.architecture_object_state);
            c.kind=ObjectTradeSessionCommandKind::purchase;
            const auto invalid=full_adapter.execute(c);
            require(invalid.state==ObjectTradeSessionState::unsupported&&full_journal.checkpoint_path()==prior_head&&
                encode_world_snapshot(full.world)==prior_world&&
                encode_architecture_object_state(*full.architecture_object_state)==prior_architecture,
                "unproven absent controller rejects purchase without publication, scenario "+std::to_string(scenario));
        }
        full=valid_full;
        full.architecture_object_state->position_registry->complete=false;
        const auto prior=full_journal.checkpoint_path();c.kind=ObjectTradeSessionCommandKind::purchase;
        auto incomplete=full_adapter.execute(c);
        require(incomplete.state==ObjectTradeSessionState::unsupported&&full_journal.checkpoint_path()==prior&&
            !full.architecture_object_state->position_registry->complete,"incomplete source cannot be promoted or published as a successful purchase");
    }
    // Re-read original files: publication cannot modify commercial inputs.
    std::ifstream check(object_file,std::ios::binary);const std::string original((std::istreambuf_iterator<char>(check)),{});
    require(original==iff({chunk("OBJD",128,objd),chunk("OBJf",128,objf),chunk("BHAV",4096,init_with_refresh()),chunk("BHAV",4097,assignments({{0,77}})),chunk("BHAV",4098,assignments({{0,11}})),chunk("BHAV",4099,assignments({{0,12}}))}),"original resource bytes remain unchanged");
}
}
int main(){try{TemporaryDirectory dir;
    const auto run=[&](const char* name,const std::function<void()>& test){
        try{test();}catch(const std::exception& error){throw std::runtime_error(std::string(name)+": "+error.what());}
    };
    run("physical member lifecycle",[&]{physical_member_lifecycle(dir.path);});
    run("community scene reopen",[&]{community_scene_reopen(dir.path);});
    run("multipart trade matrix",[&]{multipart_trade_matrix(dir.path);});
    run("dynamic association matrix",[&]{dynamic_association_matrix(dir.path);});
    run("wall trade matrix",[&]{wall_trade_matrix(dir.path);});
    run("lifecycle and journal",[&]{lifecycle_and_journal(dir.path);});
    run("candidate relationship service",[&]{candidate_relationship_matrix(dir.path);});
    std::cout<<"object trade session adapter fixtures passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
