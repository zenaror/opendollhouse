// SPDX-License-Identifier: MPL-2.0
#include "odh/native_personal_catalog.hpp"
#include "odh/original_frontend_personal.hpp"
#include "odh/native_personal_session.hpp"
#include "odh/definition_index.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/npc_avatar.hpp"
#include "original_frontend_catalog_resources.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <sstream>
#include <iostream>
namespace {
unsigned checks=0;void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){bool bad=false;try{f();}catch(const odh::FormatError&){bad=true;}require(bad,why);}
odh::NeighborEntry record(std::int16_t nid,std::uint32_t guid){odh::NeighborRecord r{};r.version=4;r.person_mode=5;r.name="preserved serialized name";r.neighbor_id=nid;r.guid=guid;r.unknown_neg_one=-1;r.person_data.emplace(80,0);r.person_data->at(61)=21;return {1,r};}
odh::Nbrs seed(){odh::Nbrs n{};n.version=62;n.entries={{0,{}},record(2,200),{0,{}},record(4,400),{0,{}}};return n;}
std::string iff(const odh::Nbrs& n){const auto p=odh::encode_nbrs(n);std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');b+="NBRS";const auto count=static_cast<std::uint32_t>(p.size()+76);for(int sh:{24,16,8,0})b.push_back(static_cast<char>(count>>sh));b.append("\0\1\0\0",4);b.append(64,'\0');b.append(reinterpret_cast<const char*>(p.data()),p.size());return b;}
odh::OriginalFrontendPersonalSource source(const odh::Nbrs& n){const auto bytes=iff(n);std::istringstream in(bytes,std::ios::binary);return odh::read_original_frontend_personal_source(odh::View(in,bytes.size()));}
odh::NativePersonalCatalogDefinition definition(std::uint32_t ordinal,std::uint32_t guid){odh::NativePersonalCatalogDefinition d;d.provider_ordinal=ordinal;d.guid=guid;d.object_type=2;d.definition_chunk_id=128;d.source="GameData/Objects/Objects.far";d.asset="People\\NPC"+std::to_string(guid)+".iff";d.fingerprint.fill(static_cast<std::uint8_t>(guid%255+1));return d;}
std::vector<odh::NativePersonalCatalogDefinition> definitions(){return {definition(4,200),definition(8,901),definition(9,902),definition(12,903),definition(15,400)};}
odh::NativePersonalIdentity identity(const odh::NativePersonalCatalog& c,std::size_t n){const auto& d=c.definitions[n];return {c.bindings[n].neighbor_id,d.guid,d.definition_chunk_id,odh::native_personal_resource_key(d.source,d.asset),d.fingerprint};}
void matrix(){const auto raw=seed();const auto s=source(raw);const auto defs=definitions();const auto c=odh::build_native_personal_catalog(raw,s.neighborhood_identity(),defs);const auto hydrated=odh::hydrate_native_personal_neighbors(raw,c);
    require(c.bindings[0].neighbor_id==2&&c.bindings[1].neighbor_id==5&&c.bindings[2].neighbor_id==3&&c.bindings[3].neighbor_id==1&&c.bindings[4].neighbor_id==4,"exact LASTNULL allocator fills5 then3 then1 while preserving original2 and4");
    require(hydrated.entries.size()==5&&hydrated.entries[0].record->guid==903&&hydrated.entries[1].record->name==raw.entries[1].record->name,"ordinal0 is genuinely hydrated; serialized name is not overwritten");
    require(hydrated.entries[0].record->person_mode==0&&!hydrated.entries[0].record->person_data&&hydrated.entries[0].record->version==10,"new personal schema absence remains distinct from record format and zero-filled bank");
    require(odh::native_personal_catalog_name("GameData/Objects.far","People\\UPPER.Mixed.iff")=="UPPER.Mixed","source basename respects slash/backslash/lastdot and exactcase");
    require(odh::native_personal_catalog_name("UserData/Characters/User00010.iff",{})=="User00010","direct basename uses actual source filename");
    for(const auto& bad:std::vector<std::string>{"../NPC.iff","People\\..\\NPC.iff",".iff",std::string("bad\0NPC.iff",11)})rejects([&]{odh::native_personal_catalog_name("GameData/Objects.far",bad);},"malformed resource name cannot invent a neighbor name");
    const auto bytes=odh::encode_native_personal_catalog(c);require(odh::encode_native_personal_catalog(odh::decode_native_personal_catalog(bytes))==bytes,"catalog provider order and exact binding receipts persist byte-identically");
    auto wrong_slot=raw;wrong_slot.entries[1].record->neighbor_id=7;rejects([&]{odh::build_native_personal_catalog(wrong_slot,s.neighborhood_identity(),defs);},"raw NID must match original manager ordinal, never an inferred sentinel shift");
    auto tampered=c;tampered.bindings[1].neighbor_id=6;rejects([&]{odh::hydrate_native_personal_neighbors(raw,tampered);},"persisted allocation cannot point to another NID");
    tampered=c;tampered.definitions[1].provider_ordinal=4;rejects([&]{odh::validate_native_personal_catalog(tampered);},"duplicate producer ordinal refused");
    auto duplicate=defs;duplicate.push_back(definition(16,200));rejects([&]{odh::build_native_personal_catalog(raw,s.neighborhood_identity(),duplicate);},"duplicate provider GUID cannot choose resource precedence");
    rejects([&]{odh::build_native_personal_catalog(raw,s.neighborhood_identity(),defs,{5});},"LASTNULL cannot steal a protected historical NID");
    auto partial=defs;partial.resize(2);const auto pc=odh::build_native_personal_catalog(raw,s.neighborhood_identity(),partial);require(!odh::hydrate_native_personal_neighbors(raw,pc).entries[0].record,"partial hydration does not skip a remaining real first NULL");
    odh::World world;for(std::size_t n=0;n<c.definitions.size();++n){const auto i=identity(c,n);const auto id=static_cast<std::int16_t>(n+1);require(world.create(id,odh::EntityKind::avatar,{0,88,0,16})&&world.define_type(id,i.guid)&&world.set_person_data(id,31,i.neighbor_id)&&world.set_person_data(id,61,(i.neighbor_id==2||i.neighbor_id==4)?21:0)&&world.set_person_data(id,10,100+id),"positive catalog identity exists before live native personal consumers");}
    const auto resolve=[&](std::int16_t id){return odh::NativePersonalSource{identity(c,static_cast<std::size_t>(id-1)),{id,world.find(id)->generation}};};
    odh::NativePersonalState state(raw,s.neighborhood_identity(),{},&c);odh::NeighborContext view(hydrated);view.bind_native_personal_catalog(c);view.bind_personal_state(&state);
    require(view.id_for_guid(903)==std::optional<std::int16_t>{1}&&view.by_id(1)->name=="NPC903","same catalog owns positive staff lookup and original-source name before constructor");
    auto prepared=odh::prepare_original_frontend_personal_save(s,world,state,0,resolve);
    require(prepared.sources().size()==5&&prepared.preview_snapshot().version==2&&prepared.preview_snapshot().catalog.has_value()&&state.snapshot().records.empty(),"full hydrated writer cohort includes both travelers and all three new staff without mutating owner");
    require(prepared.commit(state,world),"one frontend boundary commits all exact resource identities");
    require(!state.snapshot().records.front().seed_words&&state.snapshot().records.front().writer==odh::NativePersonalWriteSource::frontend_save,"new staff durable writer preserves schema0 immutable seed absence");
    const auto wire=odh::encode_native_personal_snapshot(state.snapshot());const auto restored=odh::decode_native_personal_snapshot(wire);require(odh::encode_native_personal_snapshot(restored)==wire,"NPERv2 owns catalog even independently of current filesystem enumeration");
    odh::NativePersonalState cold(raw,s.neighborhood_identity(),{},&*restored.catalog);cold.restore(restored);require(cold.by_id(1)&&cold.by_id(1)->native_personal->values[10]==104,"cold reconstructs exact staff NID and personal values from persisted source order");
    odh::NativePersonalState legacy(raw,s.neighborhood_identity());rejects([&]{legacy.restore(restored);},"v1 owner cannot implicitly hydrate from a v2 restore");const auto old=odh::encode_native_personal_snapshot(legacy.snapshot());require(odh::decode_native_personal_snapshot(old).version==1&&!odh::decode_native_personal_snapshot(old).catalog,"v1 codec remains catalog-free and unchanged");
    auto changed=restored;changed.catalog->definitions[1].fingerprint[0]^=1;rejects([&]{odh::validate_native_personal_snapshot(changed);},"catalog content cannot disagree with an existing personal resource identity");
    changed=restored;changed.catalog->definitions[1].provider_ordinal=7;rejects([&]{odh::validate_native_personal_successor(restored,changed);},"successor cannot reshuffle admitted catalog order even if definitions remain same");
}
void constructor(){const auto raw=seed();const auto s=source(raw);const auto c=odh::build_native_personal_catalog(raw,s.neighborhood_identity(),definitions());odh::NeighborContext neighbors(odh::hydrate_native_personal_neighbors(raw,c));neighbors.bind_native_personal_catalog(c);
    odh::World world;require(world.create(90,odh::EntityKind::object)&&world.define_type(90,99999)&&world.set_position(90,{96,96,1,0}),"real source creator exists before fresh staff construction");
    odh::VmFrame frame{odh::routine_key(4100),0,{90,90,90,0},{},{}};odh::Ts1Temps temps{};
    const auto request=odh::resolve_npc_avatar_guid_request({902,static_cast<std::uint8_t>(odh::CreateObjectPosition::out_of_world),0,0,0},frame,temps,neighbors,world);
    require(request.status==odh::NpcAvatarRequestStatus::ready&&request.request&&request.request->constructor_neighbor&&request.request->constructor_neighbor->neighbor_id==3,"actual literal-GUID request consumes hydrated positive NID before body allocation");
    odh::ObjectDefinition def{};def.version=138;def.guid=902;def.object_type=2;def.num_attributes=2;def.level_offset=0;odh::EntryPointTable entries;entries.entries.resize(9);entries.entries[1].action=4097;
    require(odh::materialize_npc_avatar(world,10,*request.request,def,entries),"actual legacy-compatible staff bank construction");
    bool ran=false;odh::Bhav main{0x8002,{{15,0,253,{}}},{},odh::Bhav::Metadata{0,4,0,0}};
    odh::Vm vm(odh::RoutineSet{},world.vm_memory(),{},[&](const auto&,const auto&,const auto&,const auto&){ran=true;require(world.find(10)->person_data[31]==3&&neighbors.by_id(3)->guid==902,"positive persistent staff identity is visible at the first real Main dispatch");return odh::PrimitiveOutcome{odh::PrimitiveDisposition::continue_next_tick,{}};},odh::RoutineOwners{{77,{{odh::routine_key(4097),main}}}});
    odh::NpcAvatarBodyInputs input;input.body=odh::AvatarBodyStrings{30,0,"adult","body","head",{},"Nora",{"Nora"}};input.global_walk={odh::StringTableState::decoded,-3,{{1,"walk",{}}},0};input.global_swim={odh::StringTableState::decoded,-3,{{1,"swim",{}}},0};input.birth_time=odh::NpcAvatarBirthTime{2001,9,7,12,0};odh::RandomState random{12345};
    const auto initialized=odh::initialize_npc_avatar(world,vm,10,77,*request.request,entries,input,random,{},{});require(initialized.completed&&initialized.main_started&&!ran&&world.find(10)->person_data[31]==3,"initializer binds positive catalog identity before any actual Main run");require(vm.run(8)==odh::VmStatus::next_tick&&ran,"actual Main observes same source identity without retrofitting an old NID0 body");
}
void resources(){namespace fs=std::filesystem;const auto fixture=fs::temp_directory_path()/("odh-hydrated-catalog-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));const auto root=fixture/"game";require(fs::create_directories(root),"create isolated owned resource fixture");struct Cleanup{fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{fixture};
    const auto n=seed();const auto src=source(n);std::vector<odh::DefinitionCandidate> input;
    for(const auto guid:{901U,200U,902U,903U,400U}){std::string payload(32,'\0');const auto word=[&](std::size_t at,std::uint32_t value,unsigned count){for(unsigned i=0;i<count;++i)payload[at+i]=static_cast<char>(value>>(8*i));};word(0,138,4);word(14,128,2);word(18,2,2);word(22,65535,2);word(28,guid,4);
        std::string bytes="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";bytes.resize(64,'\0');bytes+="OBJD";const auto size=static_cast<std::uint32_t>(76+payload.size());for(int sh:{24,16,8,0})bytes.push_back(static_cast<char>(size>>sh));bytes.append("\0\x80\0\0",4);bytes.append(64,'\0');bytes+=payload;const auto path=root/("NPC"+std::to_string(guid)+".iff");{std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());}
        std::istringstream in(payload,std::ios::binary);input.push_back({path.string(),{},128,odh::read_object_definition(odh::View(in,payload.size()))});
    }
    odh::DefinitionIndex index(input);const auto c=odh_host::admit_original_frontend_catalog_resources(index,root,n,src.neighborhood_identity(),{});std::reverse(input.begin(),input.end());odh::DefinitionIndex reordered(input);const auto cold=odh_host::admit_original_frontend_catalog_resources(reordered,root,n,src.neighborhood_identity(),{},&c);require(odh::encode_native_personal_catalog(cold)==odh::encode_native_personal_catalog(c),"cold accepts exact same resources in new discovery order without reallocating persisted IDs");
    const auto installed=root/"UserData2/Characters",player=fixture/"private/Characters";
    fs::create_directories(installed);fs::create_directories(player);
    fs::copy_file(root/"NPC901.iff",installed/"NPC901.iff");fs::copy_file(root/"NPC200.iff",player/"NPC200.iff");
    auto selected=input;for(auto& d:selected){if(d.definition.guid==901)d.source=(installed/"NPC901.iff").string();if(d.definition.guid==200)d.source=(player/"NPC200.iff").string();}
    const odh_host::TrustedSessionResourceRoots roots{root,installed,player};
    odh::DefinitionIndex selected_index(selected);const auto portable=odh_host::admit_original_frontend_catalog_resources(selected_index,root,n,src.neighborhood_identity(),{},nullptr,&roots);
    const auto find_source=[&](std::uint32_t guid){for(const auto& d:portable.definitions)if(d.guid==guid)return d.source;throw std::runtime_error("missing selected fixture GUID");};
    require(find_source(901)=="UserData/Characters/NPC901.iff"&&find_source(200)=="PlayerData/Characters/NPC200.iff","selected UserData2 and external player files use established distinct portable aliases");
    require(odh::native_personal_resource_key(find_source(901),{})==odh::native_personal_resource_key("UserData2/Characters/NPC901.iff",{})&&odh::native_personal_resource_key(find_source(200),{})=="PlayerData/Characters/NPC200.iff","catalog identities agree with existing HHRD and NPER character identities");
    require(odh_host::trusted_session_source_key(roots,"UserData/Characters/NPC901.iff")==find_source(901)&&odh_host::trusted_session_source_key(roots,"PlayerData/Characters/NPC200.iff")==find_source(200),"cold aliases resolve through exact selected catalogs");
    std::reverse(selected.begin(),selected.end());odh::DefinitionIndex selected_reordered(selected);
    require(odh::encode_native_personal_catalog(odh_host::admit_original_frontend_catalog_resources(selected_reordered,root,n,src.neighborhood_identity(),{},&portable,&roots))==odh::encode_native_personal_catalog(portable),"cold external/selected roots retain bindings and bytes after provider reordering");
    const auto outside=fixture/"outside.iff";fs::copy_file(root/"NPC901.iff",outside);
    rejects([&]{odh_host::trusted_session_source_key(roots,outside.string());},"an arbitrary external resource has no trusted catalog authority");
    const auto other=root/"UserData3/Characters";fs::create_directories(other);fs::copy_file(root/"NPC901.iff",other/"NPC901.iff");
    rejects([&]{odh_host::trusted_session_source_key(roots,(other/"NPC901.iff").string());},"another installed neighborhood cannot acquire the selected alias");
    fs::create_symlink(outside,player/"escape.iff");rejects([&]{odh_host::trusted_session_source_key(roots,"PlayerData/Characters/escape.iff");},"saved aliases cannot escape through a symlink file");
    fs::create_directory_symlink(player,fixture/"linked-characters");const odh_host::TrustedSessionResourceRoots linked{root,installed,fixture/"linked-characters"};rejects([&]{odh_host::trusted_session_source_key(linked,(player/"NPC200.iff").string());},"a symlink catalog root is not authority");
    rejects([&]{odh_host::trusted_session_source_key(roots,"PlayerData/Characters/../NPC200.iff");},"saved traversal is rejected before resolving a catalog");
    const odh_host::TrustedSessionResourceRoots optional_missing{root,installed,fixture/"not-created"};require(odh_host::trusted_session_source_key(optional_missing,(root/"NPC902.iff").string())=="NPC902.iff","unused missing player catalog does not reject ordinary installed resources");
    require(odh_host::generic_session_source_key(root,(installed/"NPC901.iff").string())=="UserData2/Characters/NPC901.iff","legacy generic mapper remains byte-identical for catalogless v1 consumers");
    {std::fstream out(player/"NPC200.iff",std::ios::binary|std::ios::in|std::ios::out);out.seekp(64+76+6);out.put('x');}
    rejects([&]{odh_host::admit_original_frontend_catalog_resources(selected_reordered,root,n,src.neighborhood_identity(),{},&portable,&roots);},"cold detects changed content in an otherwise trusted external player character");
    const auto target=root/"NPC901.iff";{std::fstream out(target,std::ios::binary|std::ios::in|std::ios::out);out.seekp(64+76+6);out.put('x');}rejects([&]{odh_host::admit_original_frontend_catalog_resources(reordered,root,n,src.neighborhood_identity(),{},&c);},"valid IFF unknown-byte content change still invalidates persisted fingerprint");
    input.pop_back();odh::DefinitionIndex missing(input);rejects([&]{odh_host::admit_original_frontend_catalog_resources(missing,root,n,src.neighborhood_identity(),{},&c);},"cold cannot remove a persisted provider resource");
}
void provider(){odh::ObjectDefinition a{},b{};a.guid=900;a.object_type=2;b.guid=100;b.object_type=2;const std::vector<odh::DefinitionCandidate> input{{"one.iff",{},128,a},{"two.iff",{},128,b},{"alias.iff",{},129,a}};odh::DefinitionIndex index(input);const auto ordered=index.provider_snapshot_candidates();const auto sorted=index.snapshot_candidates();require(ordered[0].source=="one.iff"&&ordered[1].source=="two.iff"&&ordered[2].source=="alias.iff"&&sorted[0].definition.guid==100&&!index.unique(900),"new provider API preserves all actual input candidates and duplicate order without changing legacy sorted ambiguity");}
}
int main(){try{matrix();constructor();provider();resources();std::cout<<"native personal catalog: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
