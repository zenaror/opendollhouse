// SPDX-License-Identifier: MPL-2.0
#include "odh/original_frontend_personal.hpp"
#include "odh/native_personal_session.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/world_snapshot_codec.hpp"
#include <sstream>
#include <iostream>
namespace {
unsigned checks=0;
void require(bool v,const char* why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>void rejects(F f,const char* why){bool rejected=false;try{f();}catch(const odh::FormatError&){rejected=true;}require(rejected,why);}
odh::NeighborEntry neighbor(std::int16_t n,std::uint32_t guid=0){odh::NeighborRecord r{};r.version=4;r.person_mode=5;r.name="Fixture";r.neighbor_id=n;r.guid=guid?guid:10000+n;r.unknown_neg_one=-1;r.person_data.emplace(80,0);r.person_data->at(61)=21;return {1,r};}
odh::Nbrs neighbors(){odh::Nbrs n{};n.version=62;n.entries={neighbor(41),neighbor(42),neighbor(43),{0,{}},neighbor(44)};return n;}
std::string iff(const odh::Nbrs& n,bool duplicate=false){const auto p=odh::encode_nbrs(n);std::string b="IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";b.resize(64,'\0');const auto chunk=[&]{b+="NBRS";const auto s=static_cast<std::uint32_t>(76+p.size());for(int sh:{24,16,8,0})b.push_back(static_cast<char>(s>>sh));b.append("\0\1\0\0",4);b.append(64,'\0');b.append(reinterpret_cast<const char*>(p.data()),p.size());};chunk();if(duplicate)chunk();return b;}
odh::OriginalFrontendPersonalSource read(const std::string& b){std::istringstream in(b,std::ios::binary);return odh::read_original_frontend_personal_source(odh::View(in,b.size()));}
odh::NativePersonalIdentity identity(std::int16_t nid,std::uint32_t guid=0){odh::NativePersonalIdentity i{nid,guid?guid:std::uint32_t(10000+nid),128,"UserData/Characters/Fixture"+std::to_string(nid)+".iff",{}};i.character_fingerprint.fill(static_cast<std::uint8_t>(nid));return i;}
void person(odh::World& w,std::int16_t id,std::int16_t nid,std::uint32_t guid=0){require(w.create(id,odh::EntityKind::avatar,{0,88,0,16})&&w.define_type(id,identity(nid,guid).guid)&&w.set_person_data(id,31,nid)&&w.set_person_data(id,61,21)&&w.set_person_data(id,32,4)&&w.set_motive(id,9,-70),"create actual live provenance-bound body");}
odh::NativePersonalSourceResolver resolver(const odh::World& w){return [&w](std::int16_t id){const auto* e=w.find(id);if(!e||!e->definition||e->person_data.size()<32)throw odh::FormatError("fixture resolver missing body");return odh::NativePersonalSource{identity(e->person_data[31],e->definition->guid),{id,e->generation}};};}
void boundary_matrix(){
    struct Case{std::int16_t house,simless;std::optional<std::int32_t> family;std::optional<std::int16_t> zoning;std::optional<bool> result;};
    const std::vector<Case> cases{
        {20,0,5,1,false},{21,0,5,1,true},{30,0,5,1,true},{31,0,5,1,false},
        {80,0,5,1,false},{81,0,5,1,true},{89,0,5,1,true},{90,0,5,1,false},
        {92,0,5,1,false},{93,0,5,1,true},{98,0,5,1,true},{99,0,5,1,false},
        {39,0,0,1,false},{40,0,0,1,true},{49,0,0,1,true},{50,0,0,1,false},
        {40,0,5,1,false},{49,0,5,1,false},{40,0,65536,1,false},
        {61,0,5,2,true},{61,0,5,1,false},{61,0,5,0,false},
        {21,1,0,2,false},{81,-1,0,2,false},{93,2,0,2,false},{40,1,0,2,false},
        {21,0,{},{},true},{81,0,{},{},true},{93,0,{},{},true},
        {40,0,0,{},true},{40,0,{},2,true},{40,0,{},1,{}},{40,0,5,{},{}},
        {61,0,{},2,true},{61,0,{},1,false},{61,0,5,{},{}},{61,1,{},{},false}
    };
    for(const auto& c:cases){const auto zoning=c.zoning?std::optional<odh::OriginalLotClass>{static_cast<odh::OriginalLotClass>(*c.zoning)}:std::nullopt;
        require(odh::original_frontend_personal_save_required({c.house,c.simless,c.family,zoning})==c.result,
            "source-derived frontend boundary matrix preserves exact range edges, playing/family/zoning and necessary unknowns");}
    const odh::LotZoning rows{{5,0},{61,1},{62,7}};
    require(odh::classify_original_lot(nullptr,0)==odh::OriginalLotClass::residential&&
        !odh::classify_original_lot(nullptr,5)&&odh::classify_original_lot(&rows,0)==odh::OriginalLotClass::residential,
        "source key0 is residential without rows; missing authority stays distinct");
    require(odh::classify_original_lot(&rows,5)==odh::OriginalLotClass::residential&&
        odh::classify_original_lot(&rows,61)==odh::OriginalLotClass::community&&
        odh::classify_original_lot(&rows,6)==odh::OriginalLotClass::unclassified&&!odh::classify_original_lot(&rows,62),
        "raw native labels map explicitly to source classes, preserving known absence and rejecting unsupported row domains");
    require(odh::original_frontend_personal_save_required({61,0,5,odh::classify_original_lot(&rows,61)})==std::optional<bool>{true}&&
        odh::original_frontend_personal_save_required({5,0,5,odh::classify_original_lot(&rows,5)})==std::optional<bool>{false}&&
        odh::original_frontend_personal_save_required({6,0,5,odh::classify_original_lot(&rows,6)})==std::optional<bool>{false},
        "producer-to-policy integration distinguishes residential, community and missing-row HOME without an invented fallback");
    // The classifier has no mode enum: a community row remains authoritative
    // outside any hard-coded map, and editor/nonplaying cannot capture people.
    for(const auto house:{std::int16_t{5},std::int16_t{49},std::int16_t{62},std::int16_t{91}})
        require(odh::original_frontend_personal_save_required({house,0,5,odh::OriginalLotClass::community})==std::optional<bool>{true}&&
            odh::original_frontend_personal_save_required({house,1,0,odh::OriginalLotClass::community})==std::optional<bool>{false},
            "actual community zoning and playing state take precedence over selector labels");
}
void parsed_boundary_writer_matrix(){
    odh::StringTable zoning_text{odh::StringTableState::decoded,-3,{{1,"61, community",{}},{1,"60, residential",{}}},0};
    const auto zoning=odh::read_lot_zoning(zoning_text);
    struct Case{std::int16_t house;std::int32_t family;bool capture;};
    for(const auto c:std::vector<Case>{{22,5,true},{61,5,true},{81,5,true},{93,5,true},{40,0,true},{40,5,false},{5,5,false}}){
        const auto n=neighbors();const auto source=read(iff(n));odh::NativePersonalState state(n,source.neighborhood_identity());
        odh::World world;person(world,1,41);require(world.set_person_data(1,10,777),"controlled fixture has real personal-bank progress");
        const auto before=odh::encode_native_personal_snapshot(state.snapshot());
        const auto decision=odh::original_frontend_personal_save_required({c.house,0,c.family,odh::classify_original_lot(&zoning,c.house)});
        require(decision==std::optional<bool>{c.capture},"parsed semantic zoning feeds typed original class and exact expansion predicate");
        if(*decision){auto prepared=odh::prepare_original_frontend_personal_save(source,world,state,0,resolver(world));
            require(prepared.sources().size()==1&&state.snapshot().records.empty()&&prepared.commit(state,world)&&
                state.snapshot().records.size()==1&&state.snapshot().records.front().revision==1&&
                state.snapshot().records.front().writer==odh::NativePersonalWriteSource::frontend_save&&
                state.snapshot().records.front().projection.values[10]==777,
                "Downtown/community/Studio/Magic/Vacation0 use the SAME authenticated writer and one real revision");
        }else require(odh::encode_native_personal_snapshot(state.snapshot())==before,
            "ordinary HOME and nonzero-family Vacation do not fabricate writer4; their other persistence branch remains separate");
    }
}
void matrix(){auto n=neighbors();auto bytes=iff(n);const auto s=read(bytes);bytes.assign(1,'x');
    odh::NativePersonalState state(n,s.neighborhood_identity());odh::World w;for(std::int16_t i=1;i<=4;++i)person(w,i,40+i);
    const auto raw=odh::encode_world_snapshot(w.snapshot());const auto resolve=resolver(w);
    for(auto mode:{std::int16_t(1),std::int16_t(-1),std::int16_t(2)})rejects([&]{odh::prepare_original_frontend_personal_save(s,w,state,mode,resolve);},"simless/nonplaying mode is not coerced to source playing0");
    auto plan=odh::prepare_original_frontend_personal_save(s,w,state,0,resolve);
    require(plan.sources().size()==3&&plan.preview_snapshot().records.size()==3&&!state.changed(),"two traveler fixtures plus unrelated live NPC admitted before first-null; prepare unpublished");
    require(odh::encode_world_snapshot(w.snapshot())==raw,"preparation leaves World and live needs/workspace intact");
    const auto preview=odh::encode_native_personal_snapshot(plan.preview_snapshot());
    require(plan.source_current(w,state)&&plan.commit(state,w),"one explicit frontend boundary commits the prepared cohort");
    require(odh::encode_native_personal_snapshot(state.snapshot())==preview&&!plan.commit(state,w),"one prepared receipt cannot duplicate writer revisions");
    require(state.snapshot().records.size()==3&&state.snapshot().records[0].writer==odh::NativePersonalWriteSource::frontend_save&&!state.by_id(44),"post-null live neighbor excluded; writer is distinct from family17");
    for(auto writer:{odh::NativePersonalWriteSource::generic25,odh::NativePersonalWriteSource::retirement,odh::NativePersonalWriteSource::family17,odh::NativePersonalWriteSource::frontend_save}){auto wire=state.snapshot();for(auto& r:wire.records)r.writer=writer;const auto b=odh::encode_native_personal_snapshot(wire);require(odh::encode_native_personal_snapshot(odh::decode_native_personal_snapshot(b))==b,"existing writer bytes and appended writer4 codec roundtrip");}
    auto invalid=state.snapshot();invalid.records[0].writer=static_cast<odh::NativePersonalWriteSource>(5);rejects([&]{odh::encode_native_personal_snapshot(invalid);},"unknown appended writer is refused");
    const auto before=odh::encode_native_personal_snapshot(state.snapshot());auto stale=odh::prepare_original_frontend_personal_save(s,w,state,0,resolve);
    require(w.set_person_data(2,10,123),"actual progress after preparation");require(!stale.source_current(w,state)&&!stale.commit(state,w)&&odh::encode_native_personal_snapshot(state.snapshot())==before,"one changed batch member prevents all revisions atomically");
    auto lifetime=odh::prepare_original_frontend_personal_save(s,w,state,0,resolve);const odh::WorldEntityReference old{3,w.find(3)->generation};require(w.mark_dead(old)&&w.remove_retaining_body(old),"retire an actual source generation while preserving body");require(!lifetime.commit(state,w),"retained body cannot stand in for live frontend source");
    person(w,3,43);require(w.find(3)->generation!=old.generation&&!lifetime.source_current(w,state),"same numeric ID with new generation cannot revive old preparation");
    bool resource_ok=true;const auto guarded=[&](std::int16_t id){auto r=resolve(id);if(!resource_ok&&id==2)r.identity.character_fingerprint[0]^=1;return r;};auto resource=odh::prepare_original_frontend_personal_save(s,w,state,0,guarded);resource_ok=false;require(!resource.commit(state,w)&&odh::encode_native_personal_snapshot(state.snapshot())==before,"changed immutable resource identity cannot publish a stale prepared batch");
    const auto wrong=[&](std::int16_t id){auto r=resolve(id);++r.identity.guid;return r;};rejects([&]{odh::prepare_original_frontend_personal_save(s,w,state,0,wrong);},"resolver cannot substitute another character");
    auto malformed=iff(n);malformed.pop_back();rejects([&]{read(malformed);},"truncated immutable source refused");rejects([&]{read(iff(n,true));},"duplicate NBRS source refused");
}
void index_matrix(){auto n=neighbors();n.entries={neighbor(41),{0,{}},neighbor(44,10041)};auto s=read(iff(n));odh::NativePersonalState state(n,s.neighborhood_identity());odh::World w;person(w,1,41);person(w,4,44,10041);const auto resolve=resolver(w);
    odh::NeighborContext legacy(n);require(legacy.id_for_guid(10041)==std::optional<std::int16_t>{44},"fixture distinguishes legacy last-GUID map from original index authority");
    rejects([&]{odh::prepare_original_frontend_personal_save(s,w,state,0,resolve);},"ambiguous GUID without authenticated original index is explicitly unsupported");
    const odh::OriginalFrontendGuidLookup first=[](std::uint32_t guid)->std::optional<std::int16_t>{return guid==10041?std::optional<std::int16_t>{41}:std::nullopt;};
    auto p=odh::prepare_original_frontend_personal_save(s,w,state,0,resolve,first);require(p.sources().size()==1&&p.sources()[0].body.id==1&&p.commit(state,w),"explicit admitted original first catalog match chooses NID41, never legacy last44");
    const odh::OriginalFrontendGuidLookup bad=[](auto)->std::optional<std::int16_t>{return 42;};rejects([&]{odh::prepare_original_frontend_personal_save(s,w,state,0,resolve,bad);},"catalog index cannot authorize another source GUID");
    auto repeated=n;repeated.entries={neighbor(41),neighbor(44,10041)};const auto repeated_source=read(iff(repeated));odh::NativePersonalState repeated_state(repeated,repeated_source.neighborhood_identity());
    rejects([&]{odh::prepare_original_frontend_personal_save(repeated_source,w,repeated_state,0,resolve,first);},"repeated mapped writer rows are explicitly unsupported, never silently deduplicated");
    auto other=n;other.reserved=1;const auto other_source=read(iff(other));rejects([&]{odh::prepare_original_frontend_personal_save(other_source,w,state,0,resolve,first);},"same-looking records from another immutable neighborhood digest refused");
    odh::World empty;auto e=odh::prepare_original_frontend_personal_save(s,empty,state,0,resolver(empty));const auto before=odh::encode_native_personal_snapshot(state.snapshot());require(e.sources().empty()&&e.commit(state,empty)&&!e.commit(state,empty)&&odh::encode_native_personal_snapshot(state.snapshot())==before,"empty live cohort is a single-use no-op without invented revisions");
}
void projection(){auto n=neighbors();const auto s=read(iff(n));odh::NativePersonalState state(n,s.neighborhood_identity());odh::World w;person(w,1,41);person(w,2,42);person(w,3,43);
    odh::SessionBundle home;home.world=w.snapshot();
    for(std::int16_t id:{1,2}){const auto i=identity(40+id);home.household_roster.push_back({id,w.find(id)->generation,i.guid,i.neighbor_id,i.definition_chunk_id,i.character_asset,i.character_fingerprint});}
    for(std::int16_t id:{1,2,3})require(w.set_person_data(id,10,static_cast<std::int16_t>(id*100))&&w.set_person_data(id,13,700),"source progress and constructor preferences share original selective writer");
    const auto source_world=odh::encode_world_snapshot(w.snapshot());auto p=odh::prepare_original_frontend_personal_save(s,w,state,0,resolver(w));const auto staged=p.preview_snapshot();odh::project_native_personal_session(home,staged);
    require(!state.changed()&&home.world.entities[0].person_data[10]==100&&home.world.entities[1].person_data[10]==200&&home.world.entities[0].person_data[13]==700,"unpublished HOME preview applies exact selective records to both permanent bodies");
    require(home.world.entities[0].person_data[32]==4&&home.world.entities[0].motives[9]==-70&&odh::encode_world_snapshot(w.snapshot())==source_world,"HOME thread/needs/workspace ownership and source World remain unchanged");
    require(p.commit(state,w),"publish shared writer after preview validation without simulation");odh::NativePersonalState cold(n,s.neighborhood_identity());cold.restore(odh::decode_native_personal_snapshot(odh::encode_native_personal_snapshot(state.snapshot())));require(cold.by_id(41)->native_personal->values[10]==100&&cold.by_id(43)->native_personal->values[10]==300,"cold shared owner preserves both travelers and independently admitted NPC");
    const auto projected=odh::encode_world_snapshot(home.world);odh::project_native_personal_session(home,cold.snapshot());require(odh::encode_world_snapshot(home.world)==projected,"same committed projection is idempotent and cannot rerun constructor");
}
}
int main(){try{boundary_matrix();parsed_boundary_writer_matrix();matrix();index_matrix();projection();std::cout<<"original frontend personal: "<<checks<<" checks\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
