// SPDX-License-Identifier: MPL-2.0
#include "household_placement_bindings.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <iostream>
#include <stdexcept>
#include <map>
#include <cstring>
#include <chrono>
#include <fstream>
namespace {
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::shared_ptr<odh::AnimationHeader> animation(){auto a=std::make_shared<odh::AnimationHeader>();a->name="synthetic-reach";
    odh::BoneMotion motion{};motion.frames=8;motion.time_properties={{{0,{{"xevt","0"}}}}};a->motions.push_back(motion);return a;}
void live_wall_inputs(){
    odh::World world;odh::SavedArchitecture architecture;architecture.width=architecture.height=4;
    for(auto& level:architecture.levels){level.floors.assign(16,1);level.walls.resize(16);}
    const auto object=[&](std::int16_t id,std::uint32_t guid,std::uint16_t flags){
        require(world.create(id,odh::EntityKind::object)&&world.define_type(id,guid)&&world.set_position(id,{24,24,1,0})&&
            world.declare_object_word(id,40,static_cast<std::int16_t>(flags))&&world.declare_object_word(id,13,1),"live wall fixture source fields");
    };
    object(2,100,0);const auto old_generation=world.find(2)->generation;
    std::vector<odh::WallAppearanceInput> inputs{{2,old_generation,100,odh::EntityKind::object,world.find(2)->position,false,0,1,{}}};
    require(odh_host::live_household_wall_edges(world,architecture,inputs,1).status==odh::WallEdgeBuildStatus::ready,"ordinary inventory is not wall geometry");
    require(world.remove(2),"remove ordinary source identity");object(2,200,0);
    require(world.find(2)->generation!=old_generation&&
        odh_host::live_household_wall_edges(world,architecture,inputs,1).status==odh::WallEdgeBuildStatus::ready,
        "unrelated ordinary ID reuse cannot invalidate wall geometry");
    require(world.declare_object_word(2,40,static_cast<std::int16_t>(0x8000)),"new identity becomes architectural");
    const auto new_arch=odh_host::live_household_wall_edges(world,architecture,inputs,1);
    require(new_arch.status==odh::WallEdgeBuildStatus::unsupported&&new_arch.diagnostic.find("no exact binding id=2")!=std::string::npos,
        "old ordinary snapshot cannot hide replacement architectural owner");
    require(world.declare_object_word(2,40,0),"restore ordinary replacement");
    object(3,300,0x8000);architecture.levels[0].walls[5].segments=2;
    inputs.push_back({3,world.find(3)->generation,300,odh::EntityKind::object,world.find(3)->position,false,0x8000,1,
        odh::ObjdWallAppearance{138,300,1,0,164,0}});
    const auto bound=odh_host::live_household_wall_edges(world,architecture,inputs,1);
    require(bound.status==odh::WallEdgeBuildStatus::ready&&bound.edges.at(5).top_right==odh::WallEdgeKnowledge::non_solid,
        "exact authenticated architectural binding still opens its source wall face");
    require(world.declare_object_word(3,40,0),"source mutable flags can remove architectural role");
    const auto cleared=odh_host::live_household_wall_edges(world,architecture,inputs,1);
    require(cleared.status==odh::WallEdgeBuildStatus::ready&&cleared.edges.at(5).top_right==odh::WallEdgeKnowledge::solid,
        "current architectural flags are re-read instead of frozen");
    require(world.remove(3),"remove known architectural source");
    require(odh_host::live_household_wall_edges(world,architecture,inputs,1).status==odh::WallEdgeBuildStatus::unsupported,
        "removed architectural owner requires explicit current binding event");
    object(3,300,0x8000);
    require(odh_host::live_household_wall_edges(world,architecture,inputs,1).status==odh::WallEdgeBuildStatus::unsupported,
        "reused architectural ID cannot borrow old generation binding");
    odh::ArchitectureObjectState events;events.object_support=odh::make_architecture_object_support(4,4);
    for(auto& occupied:events.occupied_walls)occupied.assign(16,0);
    odh::ObjectPositionRegistrySnapshot current;current.width=current.height=4;current.complete=true;
    events.position_registry=current;
    // Typed event-owner input to the pure geometry consumer; this is not a
    // synthetic save or a claim that this fixture executed the original VM.
    events.wall_appearance.faces.push_back({1,1,1,odh::wall_top_right,{1,0,{}},3,3,
        world.find(3)->generation,world.find(3)->generation,true,true});
    auto from_events=odh_host::live_household_wall_edges(world,architecture,events,1);
    require(from_events.status==odh::WallEdgeBuildStatus::ready&&from_events.edges.at(5).top_right==odh::WallEdgeKnowledge::non_solid,
        "explicit current-generation wall events govern geometry instead of obsolete imported input bindings");
    events.wall_appearance.faces.front().generation=old_generation;
    require(odh_host::live_household_wall_edges(world,architecture,events,1).status==odh::WallEdgeBuildStatus::unsupported,
        "current event owner cannot borrow another generation");
    events.wall_appearance.faces.front()={1,1,1,odh::wall_top_right,{0,0,{}},0,0,0,0,false,true};
    from_events=odh_host::live_household_wall_edges(world,architecture,events,1);
    require(from_events.status==odh::WallEdgeBuildStatus::ready&&from_events.edges.at(5).top_right==odh::WallEdgeKnowledge::solid,
        "explicit retired door clear restores solid geometry without requiring its former owner");
    events.position_registry->complete=false;
    require(odh_host::live_household_wall_edges(world,architecture,events,1).status==odh::WallEdgeBuildStatus::unsupported,
        "incomplete AOBJ registry never falls back to obsolete imported geometry");
}
void wall_query_diagnostics(){
    for(const bool unknown_inputs:{true,false}){
        odh::World world;require(world.create(1,odh::EntityKind::avatar)&&world.create(2,odh::EntityKind::object)&&
            world.set_position(1,{24,24,1,0})&&world.set_position(2,{24,24,1,0}),"actual wall query identities");
        odh::SavedArchitecture raw;raw.width=raw.height=4;
        odh::PreparedArchitecture prepared;prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
        for(unsigned n=0;n<2;++n){raw.levels[n].floors.assign(16,1);raw.levels[n].walls.resize(16);
            for(auto& tile:raw.levels[n].walls)tile.segments=3;
            prepared.levels[n].floors=raw.levels[n].floors;prepared.levels[n].walls=raw.levels[n].walls;}
        const auto rooms=odh::generate_prepared_room_map(prepared);
        odh::AvatarPositionValid avatar(world,{});odh::HouseholdPlacementBindings bindings{world,prepared,rooms,avatar};
        bindings.source_architecture=&raw;
        bindings.wall_edges=[&](auto)->std::optional<std::vector<odh::ResolvedWallEdges>>{
            if(unknown_inputs)return {};return std::vector<odh::ResolvedWallEdges>(16);
        };
        bindings.wall_diagnostic=[]{return std::string{"synthetic architectural identity changed"};};
        odh::HouseholdPlacementAdapter adapter(std::move(bindings));
        odh::SlotRecord slot{};slot.type=3;slot.standing=1;slot.snap_target_slot=-1;slot.facing=-2;slot.resolution=16;
        float offset=16;std::memcpy(&slot.offset_bits[0],&offset,sizeof offset);
        odh::Ts1PlacementLocations locations;odh::Vm* executing=nullptr;
        odh::Vm vm({{odh::routine_key(4096),{0x8002,{{46,254,255,{}}},{},odh::Bhav::Metadata{0,4,4,0}}}},world.vm_memory(),{},
            [&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                odh::Ts1PlacementInvocation invocation{frame,instruction,dispatch,[&]{return executing->matches_active_primitive(frame,instruction,dispatch);}};
                locations=adapter.locations(invocation,slot,{2,world.find(2)->generation},{1,world.find(1)->generation});
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};
            });executing=&vm;
        require(vm.start(odh::routine_key(4096),{1,1,2,0})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::returned_true,"wall diagnostics use actual source invocation");
        require(!locations.supported&&locations.diagnostic.find(unknown_inputs?"stage=wall-inputs":"stage=raycast")!=std::string::npos&&
            locations.diagnostic.find("extent=4x4")!=std::string::npos&&locations.diagnostic.find("caller=1:")!=std::string::npos&&
            locations.diagnostic.find("target=2:")!=std::string::npos&&locations.diagnostic.find("routine=4096 pc=0 opcode=46")!=std::string::npos,
            ("wall failure names provider stage, geometry and actual source identities/context: "+locations.diagnostic).c_str());
    }
}
void catalog_admission(){
    odh::ObjectDefinition object{};object.version=138;object.guid=100;
    const odh::DefinitionCandidate definition{"/nonexistent-read-prohibited.iff","",128,object};
    odh::DefinitionIndex definitions({definition});
    std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');
    odh_host::HouseholdPlacementSlotCatalog catalog(definitions,{"", "",std::make_shared<const std::string>(bytes)});
    odh::World world;
    for(std::int16_t id=1;id<=3;++id)require(world.create(id,odh::EntityKind::object)&&world.define_type(id,100),"dynamic catalog object identity");
    auto table=std::make_shared<odh::SlotTable>();table->version=10;table->records.push_back({});table->records[0].type=3;
    odh::ObjdBinding binding{138,100,0,128,0,0};
    const odh::Ts1PlacementIdentity first{1,world.find(1)->generation};
    require(!catalog.bind_cached(world,first)&&catalog.bind_created(world,first,definition,binding,table)&&
        catalog.bind_cached(world,{2,world.find(2)->generation}),"decoded new GUID and same-GUID new ID bind without any file reads");
    const auto lookup=[&](std::int16_t id){odh::Ts1SlotLookup result;odh::Vm* active=nullptr;
        odh::Vm vm({{odh::routine_key(4096),{0x8002,{{46,254,255,{}}},{},odh::Bhav::Metadata{0,0,0,0}}}},world.vm_memory(),{},
            [&](const auto& f,const auto& i,const auto&,const auto& d){
                odh::Ts1PlacementInvocation inv{f,i,d,[&]{return active->matches_active_primitive(f,i,d);}};
                result=catalog.lookup(world,inv,{id,world.find(id)->generation},false);
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};});active=&vm;
        vm.start(odh::routine_key(4096),{1,1,id,0});require(vm.run(1)==odh::VmStatus::returned_true,"catalog live lookup");return result;};
    require(lookup(2).table==table,"new instance reuses exact admitted immutable SLOT bank");
    auto changed=std::make_shared<odh::SlotTable>(*table);changed->records[0].flags=99;
    require(!catalog.bind_created(world,{3,world.find(3)->generation},definition,binding,changed)&&lookup(2).table==table,
        "conflicting decoded bank cannot replace existing source identity");
    auto wrong=definition;wrong.source="/another-source.iff";
    require(!catalog.bind_created(world,{3,world.find(3)->generation},wrong,binding,table),"object admission requires catalog-selected provenance");
    require(world.remove(1)&&world.create(1,odh::EntityKind::object)&&world.define_type(1,100)&&
        !catalog.bind_created(world,first,definition,binding,table)&&catalog.bind_cached(world,{1,world.find(1)->generation}),
        "recycled ID requires actual new epoch; same source table can then be reused");
    odh::ObjectDefinition avatar{};avatar.version=138;avatar.guid=777;
    const odh::DefinitionCandidate character0{"/character0.iff","",128,avatar},character1{"/character1.iff","",128,avatar};
    odh::ObjdBinding avatar_binding{138,777,0,128,0,0};
    for(std::int16_t id=4;id<=6;++id)require(world.create(id,odh::EntityKind::avatar)&&world.define_type(id,777),"explicit character resource identity");
    require(catalog.bind_created(world,{4,world.find(4)->generation},character0,avatar_binding,table)&&
        catalog.bind_created(world,{5,world.find(5)->generation},character1,avatar_binding,table)&&
        !catalog.bind_cached(world,{6,world.find(6)->generation})&&
        catalog.bind_created(world,{6,world.find(6)->generation},character1,avatar_binding,table),
        "avatar Character overrides preserve per-resource provenance instead of guessing shared GUID precedence");
    require(lookup(4).status==odh::Ts1SlotLookupStatus::available&&lookup(6).status==odh::Ts1SlotLookupStatus::available,
        "explicit avatar resources participate in same immutable slot catalog");
}
void explicit_object_mover(){
    const auto root=std::filesystem::temp_directory_path()/("odh-mover-context-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct Cleanup {std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}} cleanup{root};
    const auto put=[](std::string& s,std::size_t at,std::uint32_t value,unsigned count,bool big=false){
        for(unsigned n=0;n<count;++n)s.at(at+n)=static_cast<char>((value>>(8*(big?count-1-n:n)))&255);};
    const auto chunk=[&](const char* type,std::uint16_t id,const std::string& data){std::string s(76,'\0');s.replace(0,4,type);
        put(s,4,76+data.size(),4,true);put(s,8,id,2,true);return s+data;};
    const auto iff=[](const std::string& data){std::string s="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";s.resize(64,'\0');return s+data;};
    const auto write=[](const auto& path,const auto& bytes){std::ofstream f(path,std::ios::binary);f.write(bytes.data(),bytes.size());require(bool(f),"mover resource fixture write");};
    std::string objd(190,'\0');put(objd,0,138,4);put(objd,18,4,2);put(objd,20,7,2);put(objd,22,3,2);put(objd,28,1002,4);
    const auto object_file=root/"part.iff",global_file=root/"Global.far";write(object_file,iff(chunk("OBJD",128,objd)));
    std::string bhav(24,'\0');put(bhav,0,0x8002,2);put(bhav,2,1,2);put(bhav,12,2,2);bhav[14]=char(254);bhav[15]=char(255);
    const auto global=iff(chunk("BHAV",256,bhav));const std::string entry="global.iff";
    std::string far="FAR!byAZ";far.resize(16,'\0');put(far,8,1,4);put(far,12,16+global.size(),4);far+=global;
    const auto index_at=far.size();far.resize(index_at+20,'\0');put(far,index_at,1,4);put(far,index_at+4,global.size(),4);
    put(far,index_at+8,global.size(),4);put(far,index_at+12,16,4);put(far,index_at+16,entry.size(),4);far+=entry;write(global_file,far);
    std::istringstream definition_stream(objd,std::ios::binary);
    odh::DefinitionIndex definitions({{object_file.string(),"",128,odh::read_object_definition(odh::View(definition_stream,objd.size()))}});
    odh_host::ObjectTradeResourceLibrary resources(root,definitions,global_file,entry);resources.load(1002);
    odh::World world;odh::PreparedArchitecture prepared;prepared.width=prepared.height=4;prepared.buildable.assign(16,1);
    odh::SavedArchitecture raw;raw.width=raw.height=4;raw.altitude=std::vector<std::array<std::uint8_t,4>>(16);
    for(unsigned n=0;n<2;++n){raw.levels[n].floors.assign(16,1);raw.levels[n].walls.resize(16);
        prepared.levels[n].floors=raw.levels[n].floors;prepared.levels[n].walls=raw.levels[n].walls;}
    const auto rooms=odh::generate_prepared_room_map(prepared);
    require(world.set_room_resolver([&](const auto& p){return odh::geometric_room_at_runtime(rooms,p.x,p.y,p.level);}),"mover room resolver");
    for(std::int16_t id=1;id<=3;++id){require(world.create(id,odh::EntityKind::object,{0,0,1})&&world.define_type(id,1000+id)&&
        world.define_entry_points(id,std::vector<odh::EntryPoint>(27)),"mover distinct creator, physical part and container");
        for(std::int16_t n=0;n<=78;++n)if(n!=2&&n!=3&&n!=11&&n!=67)require(world.declare_object_word(id,n,0),"mover metadata");
        require(world.set_position(id,{id==3?40:24,24,1,0}),"mover source pose");}
    const odh::Ts1PlacementIdentity mover{2,world.find(2)->generation},container{3,world.find(3)->generation};
    const odh::ObjectGroup physical{1002,false,2,{{2,mover.generation,48,0,0}}};
    odh::AvatarPositionValid avatar(world,{});odh::HouseholdPlacementBindings bindings{world,prepared,rooms,avatar};
    bindings.source_architecture=&raw;unsigned factories=0,legacy=0;bool stale=false,candidate=false;odh::World foreign;
    bindings.wall_edges=[](auto)->std::optional<std::vector<odh::ResolvedWallEdges>>{return std::vector<odh::ResolvedWallEdges>(16);};
    bindings.destination_conflict=[](const auto&,auto,const auto&)->std::optional<bool>{return false;};
    bindings.object_context=[&](const auto&)->odh_host::SessionObjectPositionContext{++legacy;throw std::runtime_error("legacy creator factory must not select moving part");};
    bindings.slot_lifecycle=[&](const auto&)->odh_host::SessionObjectPositionServices{++legacy;throw std::runtime_error("legacy slot factory must not select moving part");};
    bindings.object_context_for=[&](const auto& invocation,auto actual){
        require(invocation.frame.context.caller==1&&invocation.frame.context.stack_object==3&&invocation.frame.context.code_owner==777&&
            actual.id==mover.id&&actual.generation==mover.generation,"explicit mover keeps genuine creator/CodeOwner and its own generation");++factories;
        odh_host::SessionObjectPositionContext context;context.world=&world;context.resources=&resources;
        context.prepared=&prepared;context.architecture=&raw;context.rooms=&rooms;context.source_still_matches=invocation.source_still_matches;
        context.group=[&](auto id)->std::optional<odh::ObjectGroup>{if(id.id!=2||id.generation!=mover.generation)return {};
            auto group=physical;if(stale)group.members[0].generation++;return group;};
        context.admit_group_member=[](auto)->std::optional<bool>{return true;};
        if(candidate)context.world=&foreign;return context;};
    odh::HouseholdPlacementAdapter adapter(std::move(bindings));odh::Vm* active=nullptr;unsigned phase=0;
    const odh::RoutineSet routines{{odh::routine_key(4096),{0x8002,{{46,254,255,{}}},{},odh::Bhav::Metadata{0,0,0,0}}}};
    odh::Vm vm(routines,world.vm_memory(),{},
        [&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
            odh::Ts1PlacementInvocation invocation{frame,instruction,dispatch,[&]{return active->matches_active_primitive(frame,instruction,dispatch);}};
            if(phase==2){odh::SlotRecord slot{};slot.type=3;slot.standing=1;slot.snap_target_slot=-1;slot.facing=-2;slot.resolution=16;
                const auto locations=adapter.locations(invocation,slot,container,mover);
                require(!locations.supported&&locations.diagnostic.find("object candidate factory borrowed another World")!=std::string::npos,
                    ("explicit candidate mover: "+locations.diagnostic).c_str());
                return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};}
            const auto result=phase==0?adapter.transfer(invocation,container,0,mover):adapter.place(invocation,mover,{},0);
            require(result.status==(stale?odh::Ts1PlacementStatus::unsupported:odh::Ts1PlacementStatus::success),
                ("explicit mover operation: "+result.diagnostic).c_str());
            return odh::PrimitiveOutcome{odh::PrimitiveDisposition::goto_true,{}};},odh::RoutineOwners{{777,routines}});active=&vm;
    const auto run=[&]{require(vm.start(odh::routine_key(4096),{1,1,3,777})==odh::VmStatus::running&&
        vm.run(1)==odh::VmStatus::returned_true,"mover factory actual source invocation");};
    run();require(world.find(2)->attachment&&world.find(2)->attachment->container==3,"new group part transfers independently of creator");
    phase=1;run();require(!world.find(2)->position&&!world.find(2)->attachment,"new direct subpart moves OOW through physical group authority");
    stale=true;run();require(factories==3&&legacy==0,"stale member rejected without retrying creator factory");
    stale=false;candidate=true;phase=2;require(world.set_position(2,{24,24,1,0}),"candidate mover source pose");run();
    require(factories==4&&legacy==0,"candidate uses exact moving identity and retains borrowed-World rejection");
}
void tests(){
    odh::World world;require(world.create(1,odh::EntityKind::avatar,{0,88,3,16})&&world.create(2,odh::EntityKind::object,{0,0,2})&&
        world.create(3,odh::EntityKind::object),"adapter World");
    for(std::int16_t id=1;id<=3;++id)require(world.set_position(id,{id==1?104:120,id==1?104:120,1,0})&&
        world.declare_object_word(id,40,0),"position/register exact objects");
    require(world.declare_object_word(1,42,0)&&world.declare_object_word(1,8,0)&&world.declare_object_word(1,52,0)&&
        world.declare_object_word(1,54,0)&&world.define_type(1,1001)&&world.define_entry_points(1,std::vector<odh::EntryPoint>(27)),"actual avatar placement metadata");
    odh::SavedArchitecture raw;raw.width=raw.height=16;odh::PreparedArchitecture prepared;prepared.width=prepared.height=16;
    for(unsigned n=0;n<2;++n){raw.levels[n].floors.assign(256,1);raw.levels[n].walls.resize(256);
        prepared.levels[n].floors=raw.levels[n].floors;prepared.levels[n].walls=raw.levels[n].walls;}
    const auto rooms=odh::generate_saved_room_map(raw);
    require(world.set_room_resolver([&](const auto& p){return odh::geometric_room_at_runtime(rooms,p.x,p.y,p.level);}),"bind room publication to same actual geometry");
    odh::AvatarPositionValidServices positioning;
    positioning.architecture=[&](auto,const auto& p)->std::optional<odh::AvatarPositionArchitecture>{const auto floor=odh::prepared_precise_floor(prepared,p.x,p.y,p.level);
        if(!floor)return {};return odh::AvatarPositionArchitecture{false,false,*floor,std::int16_t{0}};};
    positioning.intersections=[](auto,const auto&,auto){return odh::RoomInfoObstacleQuery{odh::RoomInfoObstacleIndexStatus::complete,odh::RoomInfoObstacleIndexReason::none,{}};};
    positioning.same_group=[](auto,auto)->std::optional<bool>{return false;};
    odh::AvatarPositionValid validator(world,positioning);
    auto clip=animation();odh::Playback playback(world,{}, {},[&](const auto&,auto){return clip;});
    auto slots=std::make_shared<odh::SlotTable>();slots->version=10;
    odh::SlotRecord route{};route.type=3;route.standing=1;route.snap_target_slot=-1;route.facing=-2;route.resolution=16;
    odh::SlotRecord storage{};storage.type=0;storage.height=4;slots->records={route,storage,storage};
    std::unique_ptr<odh::HouseholdPlacementAdapter> adapter;odh::VmPrimitive reach;
    odh::Vm vm({{odh::routine_key(4096),{0x8002,{{46,254,255,{0,0,3,0,1,255,255,255}}},{},odh::Bhav::Metadata{0,4,0,0}}},
        {odh::routine_key(4097),{0x8002,{{47,254,255,{1,0,255,255,0,0,255,255}}},{},odh::Bhav::Metadata{0,4,0,0}}}},
        world.vm_memory(),{},[&](const auto& frame,const auto& instruction,const auto& temps,const auto& dispatch){
            return instruction.opcode==47?reach(frame,instruction,temps,dispatch):adapter->primitive()(frame,instruction,temps,dispatch);});
    require(playback.bind(vm),"bind actual actor Playback");
    odh::HouseholdPlacementBindings bindings{world,prepared,rooms,validator};
    bindings.source_architecture=&raw;
    bindings.execution=[&](auto id)->std::optional<odh::HouseholdPlacementExecution>{
        if(id.id!=1||id.generation!=world.find(1)->generation)return {};return odh::HouseholdPlacementExecution{&vm,&playback,id.generation};};
    unsigned pre=0,post=0;
    bindings.slot_lifecycle=[&](const auto& inv){
        odh_host::SessionObjectPositionServices services;
        services.change.story_count=[]()->std::optional<std::int16_t>{return 2;};
        services.before_slot_transfer=[&,auth=inv.source_still_matches](auto item,bool){
            if(item.id==1)return auth();
            require(auth()&&item.id==3&&world.find(3)->attachment->container==2,"pre lifecycle sees exact old storage before atomic movement");
            ++pre;return world.detach(2,1);};
        services.after_slot_transfer=[&,auth=inv.source_still_matches](auto item,bool){
            if(item.id==1)return auth();
            require(auth()&&item.id==3&&world.find(3)->attachment->container==1&&world.find(3)->attachment->positioned,
                "post lifecycle sees exact new reciprocal contained state");++post;return true;};
        services.transfer_to_slot=[&,before=services.before_slot_transfer,after=services.after_slot_transfer](auto item,auto container,std::size_t slot){
            const bool done=before(item,true)&&world.attach(container.id,slot,item.id)&&world.refresh_contained_position(item.id)&&after(item,true);
            return odh_host::SessionSlotTransferResult{done,done,done?std::string{}:"fixture source slot lifecycle failed"};
        };
        return services;};
    bindings.slots=[&](const auto& inv,auto id,bool)->odh::Ts1SlotLookup{
        require(inv.source_still_matches()&&id.id==2,"resource lookup uses actual source and exact object");return {odh::Ts1SlotLookupStatus::available,slots,{}};};
    bindings.wall_edges=[](auto)->std::optional<std::vector<odh::ResolvedWallEdges>>{return std::vector<odh::ResolvedWallEdges>(256,
        {odh::WallEdgeKnowledge::non_solid,odh::WallEdgeKnowledge::non_solid});};
    bindings.destination_conflict=[](const auto& inv,auto,const auto&)->std::optional<bool>{require(inv.source_still_matches(),"route consultation is authenticated");return false;};
    adapter=std::make_unique<odh::HouseholdPlacementAdapter>(std::move(bindings));
    require(vm.start(odh::routine_key(4096),{1,2,2,0})==odh::VmStatus::running&&vm.run(10)==odh::VmStatus::returned_true&&
        world.find(1)->position->x==120&&world.find(1)->position->y==120&&
        world.find(1)->object_words.at(29)==static_cast<std::int16_t>(*odh::geometric_room_at(rooms,120,120,1)-1),
        "adapter connects resource SLOT to real geometry/PositionValid/World+Room publication");
    require(world.move_to_slot(2,1,3),"put actual Reach item in source storage slot");
    reach=adapter->reach_primitive(playback,[&](auto)->std::shared_ptr<const odh::AnimationHeader>{return clip;});
    require(vm.start(odh::routine_key(4097),{1,1,2,0},{1,0,0,0},true)==odh::VmStatus::running&&vm.run(10)==odh::VmStatus::next_tick,
        "actual47 starts through adapter at same PC");
    require(playback.advance(1)&&vm.resume_next_tick()&&vm.run(10)==odh::VmStatus::next_tick&&world.find(1)->slots[0]==3&&
        world.find(2)->slots[1]==0&&world.find(3)->attachment->container==1&&playback.carry_clip()==clip&&pre==1&&post==1,
        "actual animation event0 commits reciprocal storage transfer and prepared default carry through shared adapter");
}
void avatar_floor_lifecycle(){
    for(bool native_known:{false,true})for(std::int16_t initial_level:{1,2})for(unsigned mode=0;mode<14;++mode){
        if(initial_level==2&&mode!=2&&mode!=7&&mode!=8&&mode!=12)continue;
        const bool explicit_oow=mode==8||mode==12;
        const bool contained=mode==3||mode==4||mode==7||mode==12;
        const bool out_of_world=mode==2||mode==7||explicit_oow;
        const bool outside_lot=mode==9||mode==10||mode==11||mode==13;
        const bool blocked=mode==1||mode==3||outside_lot;
        const std::uint16_t opcode=contained?5:mode==1||mode==5?16:46;
        odh::World world;std::vector<std::string> events;std::map<std::int16_t,std::optional<odh::EntityPosition>> registered;
        odh::SavedArchitecture raw;raw.width=raw.height=4;odh::PreparedArchitecture architecture;architecture.width=architecture.height=4;
        architecture.buildable.assign(16,1);
        for(unsigned n=0;n<2;++n){raw.levels[n].floors.assign(16,1);raw.levels[n].walls.resize(16);
            architecture.levels[n].floors=raw.levels[n].floors;architecture.levels[n].walls=raw.levels[n].walls;}
        const auto rooms=odh::generate_prepared_room_map(architecture);
        require(world.set_room_resolver([&](const auto& p){return odh::geometric_room_at_runtime(rooms,p.x,p.y,p.level);}),"floor actual room publication");
        for(std::int16_t id=1;id<=4;++id){
            require(world.create(id,id==1?odh::EntityKind::avatar:odh::EntityKind::object,{0,id==1?88U:0U,1})&&
                world.define_type(id,1000+id),"floor typed avatar/container/subtree");
            for(std::int16_t n=0;n<=78;++n)if(n!=2&&n!=3&&n!=11&&n!=67)require(world.declare_object_word(id,n,0),"floor known source words");
            std::vector<odh::EntryPoint> entries(28);if(id==1){entries[9].action=4099;if(mode==6)entries[15].action=4100;}
            if(native_known)require(world.publish_native_slot0({id,world.find(id)->generation},{})&&world.declare_object_word(id,14,-1),"known native backing before physical containment");
            require(world.define_entry_points(id,entries)&&world.set_position(id,{id==2?40:24,id==2?40:24,initial_level,static_cast<std::uint8_t>(id==3?6:id==4?3:1)}),"floor original poses/entry points");
        }
        require(world.attach(1,0,3)&&world.refresh_contained_position(3)&&world.attach(3,0,4)&&world.refresh_contained_position(4),"floor actual nested carried objects");
        if(contained)require(world.attach(2,0,1)&&world.refresh_contained_position(1)&&world.refresh_contained_position(3)&&world.refresh_contained_position(4),"Drop5 actual held avatar before Pre");
        const auto old=world.find(1)->position,child_old=world.find(3)->position;
        if(explicit_oow)require(!world.place_individual_on_floor(1,odh::EntityPosition{-32768,-32768,1,6},6)&&
            world.find(1)->position==old,"World geometry guard still refuses source sentinel as a nonnull floor point");
        for(std::int16_t id=1;id<=4;++id)registered[id]=world.find(id)->position;
        unsigned checks=0,ep9=0;bool in_publish=false;
        odh::AvatarPositionValidServices valid_services;
        valid_services.architecture=[&](auto,const auto&)->std::optional<odh::AvatarPositionArchitecture>{
            ++checks;require(!registered.count(1)&&registered.count(3)&&world.find(3)->position==child_old,"PositionValid follows Pre while old child registration remains");
            return odh::AvatarPositionArchitecture{blocked,false,1,std::int16_t{0}};};
        valid_services.intersections=[](auto,const auto&,auto){return odh::RoomInfoObstacleQuery{odh::RoomInfoObstacleIndexStatus::complete,odh::RoomInfoObstacleIndexReason::none,{}};};
        valid_services.same_group=[](auto,auto)->std::optional<bool>{return false;};
        odh::AvatarPositionValid validator(world,valid_services);
        odh::HouseholdPlacementBindings bindings{world,architecture,rooms,validator};
        bindings.source_architecture=&raw;
        bindings.object_context=[&](const auto& invocation){odh_host::SessionObjectPositionContext context;context.world=&world;
            context.source_still_matches=invocation.source_still_matches;
            context.position_phase=[&](auto root,const auto& destination,auto phase,std::optional<std::uint8_t>,const std::function<bool()>& operation){
                require(root.id==1,"floor scope is actual root");
                if(out_of_world)require(!destination,"source sentinel is normalized before every lifecycle phase snapshot");
                events.push_back(phase==odh_host::SessionPositionPhase::pre?"pre":phase==odh_host::SessionPositionPhase::publish?"publish":"post");
                in_publish=phase==odh_host::SessionPositionPhase::publish;const bool result=operation();in_publish=false;return result;};return context;};
        bindings.slot_lifecycle=[&](const auto& invocation){odh_host::SessionObjectPositionServices result;
            auto& change=result.change;change.story_count=[]()->std::optional<std::int16_t>{return 2;};
            change.room_at=[&](const auto& p){return odh::geometric_room_at_runtime(rooms,p.x,p.y,p.level);};
            change.admit_single_ordinary=[](auto)->std::optional<bool>{return true;};
            change.unregister_position=[&](auto id,bool room_change){
                require(room_change==(contained||out_of_world||outside_lot||mode==6),"source roomChange comes from original geometry/container/Portal");
                events.push_back("u"+std::to_string(id.id));registered.erase(id.id);return true;};
            change.register_position=[&](auto id,std::uint16_t room,bool){
                require(world.find(id.id)->object_words.at(29)==static_cast<std::int16_t>(room-1),"Post publishes source Room29 before registration");
                if(id.id==1)require(world.find(3)->position.has_value()==world.find(1)->position.has_value()&&registered.count(3)&&registered.count(4),"avatar registration follows recursive child Post");
                registered[id.id]=world.find(id.id)->position;events.push_back("r"+std::to_string(id.id));return true;};
            change.wall_use=[](auto,bool,bool){return true;};
            change.entry_point9=[&](auto id){require(id.id==1&&world.find(1)->position&&!world.find(1)->attachment,"EP9 runs only for loose positioned avatar");++ep9;events.push_back("e1");return true;};
            change.restore_container=[&](auto item,auto parent,std::size_t slot){events.push_back("restore");return world.attach(parent.id,slot,item.id)&&world.refresh_contained_position(item.id);};
            auto valid=std::make_shared<odh::ObjectPositionValid>(world,odh::ObjectPositionValidServices{});
            auto lifecycle=std::make_shared<odh::ObjectPositionChange>(world,*valid,change);
            result.before_slot_transfer=[valid,lifecycle,authority=invocation.source_still_matches](auto id,bool room){return lifecycle->pre_position_change(id,room,authority);};
            result.after_slot_transfer=[valid,lifecycle,authority=invocation.source_still_matches](auto id,bool room){return lifecycle->position_change(id,room,authority);};
            result.lifecycle_diagnostic=[lifecycle]{return lifecycle->diagnostic();};return result;};
        odh::HouseholdPlacementAdapter adapter(std::move(bindings));
        require(world.set_spatial_change_observer([&](odh::World&,std::uint64_t){if(in_publish)require(world.find(3)->position==child_old,"scalar floor publisher does not move children before their unregister");}),"floor actual scalar observer");
        odh::Ts1PlacementResult outcome;odh::Vm* actual=nullptr;
        odh::Vm vm({{odh::routine_key(4096),{0x8002,{{opcode,254,255,{}}},{},odh::Bhav::Metadata{0,4,0,0}}}},world.vm_memory(),{},
            [&](const auto& frame,const auto& instruction,const auto&,const auto& dispatch){
                odh::Ts1PlacementInvocation invocation{frame,instruction,dispatch,[&]{return actual->matches_active_primitive(frame,instruction,dispatch);}};
                auto destination=out_of_world?std::optional<odh::EntityPosition>{}:std::optional<odh::EntityPosition>{{40,24,static_cast<std::int16_t>(mode==5?3:1),2}};
                if(explicit_oow)destination=odh::EntityPosition{-32768,-32768,1,6};
                if(mode==9)destination=odh::EntityPosition{-1,24,1,2};
                if(mode==10)destination=odh::EntityPosition{-32768,-32768,2,2};
                if(mode==11)destination=odh::EntityPosition{-32768,24,1,2};
                if(mode==13)destination=odh::EntityPosition{24,-32768,1,2};
                outcome=adapter.place(invocation,{1,world.find(1)->generation},destination,0);
                return odh::PrimitiveOutcome{outcome.status==odh::Ts1PlacementStatus::success?odh::PrimitiveDisposition::goto_true:
                    outcome.status==odh::Ts1PlacementStatus::blocked?odh::PrimitiveDisposition::goto_false:odh::PrimitiveDisposition::unsupported,outcome.diagnostic};
            });actual=&vm;
        require(vm.start(odh::routine_key(4096),{static_cast<std::int16_t>(contained?2:1),1,1,0})==odh::VmStatus::running,"floor actual5/16/46 starts");
        const auto status=vm.run(20);
        require(status==(blocked||mode==5?odh::VmStatus::returned_false:odh::VmStatus::returned_true),
            ("floor lifecycle actual5/16/46: mode="+std::to_string(mode)+" "+vm.diagnostic()+" "+outcome.diagnostic).c_str());
        if(mode==5){require(events.empty()&&checks==0&&ep9==0&&outcome.placement_error==18,"stories guard precedes every source effect");continue;}
        require(checks==(out_of_world?0U:1U)&&registered.size()==4&&ep9==(out_of_world||mode==3?0U:1U),"floor once-only check/recursive ownership/OOW EP9 suppression");
        require(events.front()=="pre"&&events[1]=="u1"&&events.back()==(ep9?"e1":"r1"),"source floor begins Pre and finishes full Post");
        require(world.find(3)->position.has_value()==world.find(1)->position.has_value()&&world.find(4)->position.has_value()==world.find(1)->position.has_value(),"floor/OOW whole subtree follows root after per-child unregister");
        if(out_of_world){
            require(world.find(1)->object_words.at(1)==(explicit_oow?6:0),"explicit OOW source heading survives normalization; null OOW keeps its original heading contract");
            for(std::int16_t id:{1,3,4})require(world.find(id)->native_out_of_world_level==std::optional<std::int16_t>{1}&&
                world.find(id)->native_slot0.has_value()==native_known,"actual avatar source OOW and recursive Post publish canonical level while preserving hierarchy knowledge");
            const auto bytes=odh::encode_world_snapshot(world.snapshot());odh::World reopened;
            require(reopened.restore_snapshot(odh::decode_world_snapshot(bytes))&&odh::encode_world_snapshot(reopened.snapshot())==bytes,
                "avatar OOW scalar and contained subtree persist through actual World codec");
        }
        if(blocked)require(world.find(1)->position==old&&world.find(1)->attachment.has_value()==contained,"blocked source restores old pose and old reciprocal slot");
        else if(!out_of_world)require(world.find(1)->position==std::optional<odh::EntityPosition>{{40,24,1,2}}&&!world.find(1)->attachment&&
            world.find(3)->position->direction==6&&world.find(4)->position->direction==3,"successful scalar root and recursive child poses keep independent headings");
    }
}
}
int main(){try{live_wall_inputs();wall_query_diagnostics();catalog_admission();explicit_object_mover();tests();avatar_floor_lifecycle();std::cout<<"household placement adapter integration passed\n";}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
