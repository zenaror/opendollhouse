// SPDX-License-Identifier: MPL-2.0
#include "odh/native_personal_session.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/world_snapshot_codec.hpp"
#include "odh/avatar_neighbor.hpp"
#include "odh/family_overlay.hpp"
#include "odh/ts1_travel_return.hpp"
#include "travel_session_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
template<class F>void rejects(F f){try{f();}catch(const odh::FormatError&){++checks;return;}throw std::runtime_error("expected FormatError");}
std::array<std::uint8_t,32> digest(){std::array<std::uint8_t,32> h{};h.fill(1);return h;}
odh::Nbrs seed(int version=10,bool present=true){
 odh::Nbrs n{};n.version=62;
 for(int id=41;id<=43;++id){odh::NeighborRecord r{};r.version=version;r.person_mode=present?1:0;r.neighbor_id=static_cast<std::int16_t>(id);r.guid=static_cast<std::uint32_t>(123415+id);r.name="Test";r.name_padding=0;r.unknown_neg_one=-1;
  if(version==10)r.unknown3=17;
  if(present){r.person_data=std::vector<std::int16_t>(version==4?80:256,23);(*r.person_data)[61]=21;if(version==10)(*r.person_data)[255]=321;}
  n.entries.push_back({1,r});
 }
 return n;
}
odh::NativePersonalIdentity identity(std::int16_t nid=41){odh::NativePersonalIdentity i;i.neighbor_id=nid;i.guid=static_cast<std::uint32_t>(123415+nid);i.definition_chunk_id=128;i.character_asset="PlayerData/Characters/User"+std::to_string(nid-40)+".iff";i.character_fingerprint.fill(static_cast<std::uint8_t>(nid+8));return i;}
void person(odh::World& w,std::int16_t id=1,std::int16_t nid=41){
 require(w.create(id,odh::EntityKind::avatar,{0,88,3,16})&&w.define_type(id,identity(nid).guid),"create authenticatable avatar");
 for(std::int16_t i=0;i<88;++i)require(w.set_person_data(id,i,static_cast<std::int16_t>(100+i)),"populate actual bank");
 require(w.set_person_data(id,31,nid)&&w.set_person_data(id,61,21)&&w.set_person_data(id,32,4)&&w.set_motive(id,9,-73),"bind identity and independent needs/role");
}
odh::NativePersonalSource source(const odh::World& w,std::int16_t id=1,std::int16_t nid=41){return {identity(nid),{id,w.find(id)->generation}};}
void matrix_resource_locators(){
 for(const auto& source:std::vector<std::string>{"UserData/Characters/User1.iff","PlayerData/Characters/User1.iff"}){
  const auto key=odh::native_personal_resource_key(source,{});require(key==source,"direct personal identity bytes unchanged");
  const auto locator=odh::native_personal_resource_locator(key);require(locator.character_catalog&&locator.source==source&&locator.asset.empty(),"direct identity stays in its authenticated catalog");
 }
 require(odh::native_personal_resource_key("UserData8/Characters/User1.iff",{})=="UserData/Characters/User1.iff","installed neighborhood character aliases retain prior logical identity");
 const std::string source="GameData/Objects/Objects.far",entry="People\\PaperCarrierF1.iff";
 const auto key=odh::native_personal_resource_key(source,entry);const auto decoded=odh::native_personal_resource_locator(key);
 require(!decoded.character_catalog&&decoded.source==source&&decoded.asset==entry,"packaged NPC locator preserves exact FAR entry bytes");
 require(odh::native_personal_resource_key(decoded.source,decoded.asset)==key,"packaged locator canonical roundtrip");
 require(odh::native_personal_resource_key("GameData/a!b","c.iff")!=odh::native_personal_resource_key("GameData/a","b!c.iff"),"source and entry separators cannot alias");
 require(odh::native_personal_resource_key(source,"People/PaperCarrierF1.iff")!=key,"FAR slash and backslash names do not silently normalize");
 for(const auto& invalid:std::vector<std::string>{"/outside.iff","C:/outside.iff","GameData/../Objects.far","GameData//Objects.far","GameData/./Objects.far","GameData\\Objects.far",std::string("GameData/a\0b",12)})
  rejects([&]{odh::native_personal_resource_key(invalid,"People\\PaperCarrierF1.iff");});
 for(const auto& invalid:std::vector<std::string>{"../People.iff","People\\..\\Carrier.iff","/People.iff","People//Carrier.iff","C:\\Carrier.iff"})
  rejects([&]{odh::native_personal_resource_key(source,invalid);});
 for(const auto& invalid:std::vector<std::string>{"Resource/v2/00/00","Resource/v1/1/00","Resource/v1/2F/00","Resource/v1/00/00","Resource/v1/abcd/00/00","UserData/Characters/../User1.iff","UserData/Characters/a/b.iff"})
  rejects([&]{odh::native_personal_resource_locator(invalid);});
 std::cout<<"direct and packaged personal resource locators passed\n";
}
void matrix_packaged_retirement(){
 odh::World world;person(world);const auto before_world=world.snapshot();const auto needs=world.find(1)->motives;
 const auto reference=odh::WorldEntityReference{1,world.find(1)->generation};auto packaged=identity();
 packaged.character_asset=odh::native_personal_resource_key("GameData/Objects/Objects.far","People\\PaperCarrierF1.iff");
 odh::NativePersonalState owner(seed(),digest());auto prepared=owner.prepare(world,{{packaged,reference}},odh::NativePersonalWriteSource::retirement);
 require(owner.commit_retirement(std::move(prepared),world,[&]{return world.retire(1);}),"packaged seeded NPC retirement publishes exact personal bank");
 require(!world.find(1)&&owner.snapshot().records.front().identity.character_asset==packaged.character_asset,"retired packaged identity is retained without a live body");
 const auto wire=odh::encode_native_personal_snapshot(owner.snapshot());odh::NativePersonalState restored(seed(),digest());restored.restore(odh::decode_native_personal_snapshot(wire));
 odh::NeighborContext neighbors(seed());neighbors.bind_personal_state(&restored);
 require(neighbors.person_data(41,10)->value==110,"future packaged constructor reads persisted personal progress through shared neighbor owner");
 odh::SessionBundle active;active.world=before_world;
 odh::SessionNpcAvatar npc; npc.origin={1,reference.generation,packaged.guid,41,"GameData/Objects/Objects.far","People\\PaperCarrierF1.iff",128,packaged.character_fingerprint};
 active.npc_avatars.push_back(npc);active.world.entities.front().person_data[10]=1;
 const auto prior=active;odh::project_native_personal_session(active,restored.snapshot());
 require(active.world.entities.front().person_data[10]==110&&active.world.entities.front().motives==needs,"exact packaged live projection updates personal data without needs mutation");
 require(active.world.entities.front().person_data[32]==4&&active.world.entities.front().generation==reference.generation,"packaged projection preserves role and body identity");
 odh::World live;require(live.restore_snapshot(prior.world)&&live.set_position(1,{24,24,1,2}),"restore packaged positioned body");
 require(odh::publish_native_personal_session(live,prior,restored.snapshot())&&live.find(1)->person_data[10]==110&&live.find(1)->position==std::optional<odh::EntityPosition>{odh::EntityPosition{24,24,1,2}},"packaged live publication preserves physical position");
 for(unsigned kind=0;kind<4;++kind){auto wrong=prior;
  if(kind==0)wrong.npc_avatars.front().origin.asset="People\\Maid.iff";
  if(kind==1)wrong.npc_avatars.front().origin.source="ExpansionPack/Objects.far";
  if(kind==2)wrong.npc_avatars.front().origin.character_fingerprint[0]^=1;
  if(kind==3)wrong.npc_avatars.front().origin.object_definition_id++;
  const auto old=odh::encode_world_snapshot(wrong.world);rejects([&]{odh::project_native_personal_session(wrong,restored.snapshot());});
  require(odh::encode_world_snapshot(wrong.world)==old&&!wrong.personal_state,"mismatched packaged resource projection remains atomic");
 }
 auto duplicate=prior;duplicate.npc_avatars.push_back(npc);rejects([&]{odh::project_native_personal_session(duplicate,restored.snapshot());});
 auto missing=packaged;missing.neighbor_id=99;missing.guid=999;odh::World outsider;require(outsider.create(1,odh::EntityKind::avatar,{0,88,3,16})&&outsider.define_type(1,999)&&outsider.set_person_data(1,31,99),"bind unmatched literal source");
 rejects([&]{restored.prepare(outsider,{{missing,{1,outsider.find(1)->generation}}},odh::NativePersonalWriteSource::retirement);});
 require(odh::encode_native_personal_snapshot(restored.snapshot())==wire,"unseeded NPC cannot manufacture persistent neighborhood identity");
 std::cout<<"packaged NPC retirement/shared restore/live projection passed\n";
}

void matrix_records(){
 for(const auto version:{4,10})for(const bool present:{false,true}){
  auto n=seed(version,present);const auto raw=odh::encode_nbrs(n);odh::NativePersonalState state(n,digest());odh::NeighborContext view(n);view.bind_personal_state(&state);odh::World w;person(w);
  const auto handler=odh::native_personal_write_primitive(w,&state,[&](auto id){return source(w,id);});
  odh::Bhav bhav{0x8002,{{1,254,255,{25,0,0,0,0,0,0,0}}},{},odh::Bhav::Metadata{0,4,4,0}};
  odh::Vm vm({{odh::routine_key(4096),bhav}},w.vm_memory(),{},handler);
  require(vm.start(odh::routine_key(4096),{1,1,1,0})==odh::VmStatus::running&&vm.run(1)==odh::VmStatus::returned_true,"actual Generic25 dispatch reaches writer");
  const auto saved=state.snapshot();require(saved.records.size()==1&&saved.records[0].writer==odh::NativePersonalWriteSource::generic25,"exact Generic25 writer provenance");
  const auto& r=saved.records[0];require(std::count(r.projection.present.begin(),r.projection.present.end(),1)==44,"exact44 fields");
  for(const auto index:{8,31,32,34,77,83,84,85,86})require(!r.projection.present[static_cast<std::size_t>(index)],"local/unsupported fields absent");
  require(r.seed_words==n.entries.front().record->person_data&&odh::encode_nbrs(n)==raw,"unknown seed words and original bytes preserved");
  const auto wire=odh::encode_native_personal_snapshot(saved);require(odh::encode_native_personal_snapshot(odh::decode_native_personal_snapshot(wire))==wire,"NPER canonical roundtrip");
  require(view.person_data(41,10)->value==110,"neighbor reads current shared owner");
  odh::World rebuilt;person(rebuilt);const auto needs=rebuilt.find(1)->motives;
  odh::inherit_neighbor(rebuilt,1,*view.by_id(41),21);
  require(rebuilt.find(1)->person_data[10]==110&&rebuilt.find(1)->person_data[80]==180&&rebuilt.find(1)->person_data[32]==4&&rebuilt.find(1)->motives==needs,"construction consumes selected progress without roles/needs");
  auto stale=wire;stale.push_back(0);rejects([&]{odh::decode_native_personal_snapshot(stale);});
  for(const auto cut:{std::size_t{0},std::size_t{4},wire.size()-1})rejects([&]{odh::decode_native_personal_snapshot({wire.begin(),wire.begin()+static_cast<std::ptrdiff_t>(cut)});});
  std::cout<<"record version="<<version<<" present="<<present<<" passed\n";
 }
}
void matrix_atomicity(){
 odh::NativePersonalState state(seed(),digest());odh::World w;person(w);person(w,2,42);person(w,3,43);
 const auto empty=odh::encode_native_personal_snapshot(state.snapshot());
 auto stale=state.prepare(w,{source(w)},odh::NativePersonalWriteSource::generic25);require(w.set_person_data(1,10,900),"change source after prepare");require(!state.commit(std::move(stale),w)&&odh::encode_native_personal_snapshot(state.snapshot())==empty,"stale source leaves owner unchanged");
 auto bad=source(w,2,42);bad.identity.guid++;rejects([&]{state.prepare(w,{source(w),bad},odh::NativePersonalWriteSource::family17);});require(!state.changed(),"invalid second member leaves whole batch unchanged");
 auto batch=state.prepare(w,{source(w),source(w,2,42)},odh::NativePersonalWriteSource::family17);require(state.commit(std::move(batch),w)&&state.snapshot().records.size()==2&&!state.by_id(43),"family writer captures explicit live list only");
 auto first=state.prepare(w,{source(w)},odh::NativePersonalWriteSource::generic25);auto second=state.prepare(w,{source(w)},odh::NativePersonalWriteSource::generic25);require(state.commit(std::move(first),w)&&!state.commit(std::move(second),w),"epoch rejects competing prepared commit");
 auto changed=source(w);changed.identity.character_fingerprint[0]++;rejects([&]{state.prepare(w,{changed},odh::NativePersonalWriteSource::generic25);});
 const auto before=odh::encode_native_personal_snapshot(state.snapshot());auto failed=state.prepare(w,{source(w)},odh::NativePersonalWriteSource::retirement);
 require(!state.commit_retirement(std::move(failed),w,[]{return false;})&&odh::encode_native_personal_snapshot(state.snapshot())==before&&w.find(1),"failed actual retirement leaves record unchanged");
 auto retirement=state.prepare(w,{source(w)},odh::NativePersonalWriteSource::retirement);require(state.commit_retirement(std::move(retirement),w,[&]{return w.retire(1);})&&!w.find(1),"actual retirement publishes captured bank once");
 require(!state.commit_retirement(std::move(retirement),w,[]{return true;}),"retirement prepared receipt cannot replay");
 auto corrupt=state.snapshot();corrupt.records.front().seed_words->back()++;rejects([&]{state.restore(corrupt);});require(odh::encode_native_personal_snapshot(state.snapshot())!=before,"rejected restore preserves last successful revision");
 std::cout<<"atomic Generic25/family17/retirement passed\n";
}
void matrix_schema(){
 odh::NativePersonalProjection p;p.schema_version=1;p.present[10]=1;p.values[10]=55;std::vector<std::int16_t> bank(88,17);odh::apply_native_personal_projection(p,bank);require(bank[10]==55&&bank[56]==17&&bank[83]==17,"selective inverse retains absent and unsupported words");
 p.present[56]=1;p.values[56]=99;rejects([&]{odh::apply_native_personal_projection(p,bank);});require(bank[56]==17,"version gate rejects before target mutation");p.schema_version=2;odh::apply_native_personal_projection(p,bank);require(bank[56]==99,"version2 job presence admitted");
 p.profile=2;rejects([&]{odh::apply_native_personal_projection(p,bank);});
 std::cout<<"schema/presence passed\n";
}
void matrix_shared(){
 auto n=seed();odh::NativePersonalState owner(n,digest());odh::World w;person(w);auto write=owner.prepare(w,{source(w)},odh::NativePersonalWriteSource::generic25);require(owner.commit(std::move(write),w),"commit shared state");
 odh::FamilyOverlay old;old.neighborhood_identity=digest();const auto legacy=odh::encode_family_overlay(old);require(legacy[4]==2,"legacy overlay remains version2");
 auto overlay=odh::plan_neighborhood_personal_overlay(digest(),&old,owner.snapshot());const auto wire=odh::encode_family_overlay(overlay);require(wire[4]==11&&odh::encode_family_overlay(odh::decode_family_overlay(wire))==wire,"explicit v11 carrier canonical roundtrip");
 odh::NativePersonalState another_family(n,digest());another_family.restore(*odh::decode_family_overlay(wire).personal_state);odh::NeighborContext another_view(n);another_view.bind_personal_state(&another_family);require(another_view.person_data(41,10)->value==110,"different family reads same neighborhood record");
 auto changed=owner.snapshot();changed.records.front().projection.values[10]++;rejects([&]{odh::plan_neighborhood_personal_overlay(digest(),&overlay,changed);});changed.records.clear();rejects([&]{odh::plan_neighborhood_personal_overlay(digest(),&overlay,changed);});
 odh::NativePersonalState tombstone(odh::Nbrs{},digest(),{41});tombstone.restore(owner.snapshot());require(!tombstone.by_id(41)&&tombstone.snapshot().records.size()==1,"explicit deletion tombstone retains history without resurrecting neighbor");
 require(odh::encode_family_overlay(old)==legacy,"planning upgrade leaves legacy bytes unchanged");
 std::cout<<"cross-family carrier/CAS/tombstone passed\n";
}
void matrix_sessions(){
 auto home=travel_test::managed_home();const auto legacy=odh::encode_session_bundle(home);require(odh::encode_session_bundle(odh::decode_session_bundle(legacy))==legacy,"zero-tick legacy remains byte exact without NPER");
 home.world.entities.front().person_data.resize(88);home.world.word_count+=8;
 odh::World w;require(w.restore_snapshot(home.world),"load home world");require(w.set_person_data(1,10,337),"actual skill change");odh::NativePersonalState owner(seed(),digest());auto commit=owner.prepare(w,{source(w)},odh::NativePersonalWriteSource::generic25);require(owner.commit(std::move(commit),w),"persist source");
 const auto prior=home;odh::project_native_personal_session(home,owner.snapshot());require(home.world.entities.front().person_data[10]==337&&home.world.entities.front().motives==prior.world.entities.front().motives,"authorized exact HOME projection preserves needs");require(home.household_roster.front().id==1&&home.world.entities.front().generation==prior.world.entities.front().generation,"existing body reused by projection");
 const auto encoded=odh::encode_session_bundle(home);require(odh::encode_session_bundle(odh::decode_session_bundle(encoded))==encoded,"NPER session roundtrip");
 auto archive=odh::decode_session_archive(encoded);for(auto& section:archive.sections)if(section.id==std::array<char,4>{{'N','P','E','R'}})section.required=false;rejects([&]{odh::decode_session_bundle(odh::encode_session_archive(archive));});
 auto wrong=owner.snapshot();wrong.neighborhood_identity[0]++;rejects([&]{odh::project_native_personal_session(home,wrong);});require(odh::encode_session_bundle(home)==encoded,"failed projection is atomic");
 home.world.entities.front().person_data[10]=444;odh::project_native_personal_session(home,owner.snapshot());require(home.world.entities.front().person_data[10]==444,"same revision does not replay over newer unsaved live progress");
 // Exercise the live consumer as well as the pure archive projection.
 odh::World positioned;require(positioned.restore_snapshot(prior.world)&&positioned.set_position(1,{24,24,1,2}),"positioned live restore fixture");
 const auto position=positioned.find(1)->position;const auto live_needs=positioned.find(1)->motives;
 require(odh::publish_native_personal_session(positioned,prior,owner.snapshot()),"live positioned resident accepts current shared progress");
 require(positioned.find(1)->person_data[10]==337&&positioned.find(1)->position==position&&positioned.find(1)->motives==live_needs,"live projection preserves physical position and needs");
 odh::World contained;require(contained.restore_snapshot(prior.world)&&contained.create(500,odh::EntityKind::object,{0,0,1})&&contained.set_position(500,{24,24,1,2})&&contained.attach(500,0,1),"contained live resident fixture");
 const auto attachment=contained.find(1)->attachment;require(attachment.has_value(),"fixture has an actual physical attachment");
 require(odh::publish_native_personal_session(contained,prior,owner.snapshot()),"live contained resident accepts current shared progress");
 require(contained.find(1)->person_data[10]==337&&contained.find(1)->attachment&&contained.find(1)->attachment->container==attachment->container&&contained.find(1)->attachment->slot==attachment->slot&&contained.find(500)->slots[0]==1,"live projection preserves reciprocal physical attachment");
 const auto unchanged=odh::encode_world_snapshot(contained.snapshot());auto invalid=owner.snapshot();invalid.records.front().identity.character_fingerprint[0]^=1;
 rejects([&]{odh::publish_native_personal_session(contained,prior,invalid);});
 require(odh::encode_world_snapshot(contained.snapshot())==unchanged,"rejected live projection leaves the complete World unchanged");
 std::cout<<"legacy/NPER archive and exact HOME projection passed\n";
}
void matrix_travel(){
 auto envelope=travel_test::departure_party_envelope();auto active=odh::decode_session_bundle(*envelope.travel_visited_session);
 odh::World source_world;require(source_world.restore_snapshot(active.world)&&source_world.set_person_data(10,10,777),"visited non-HHRD relative gains skill");
 odh::NativePersonalState owner(seed(),digest());auto prepared=owner.prepare(source_world,{source(source_world,10,42)},odh::NativePersonalWriteSource::generic25);require(owner.commit(std::move(prepared),source_world),"authorized NPC writer");
 active.world=source_world.snapshot();active.personal_state=owner.snapshot();envelope.personal_state=active.personal_state;envelope.travel_visited_session=odh::encode_session_bundle(active);
 const auto returned=odh::prepare_ts1_travel_return(envelope);require(!returned.requires_original_personal_persistence_resolution(),"authorized NPC personal change resolves with existing HOME body");
 const auto& home=returned.home();const auto found=std::find_if(home.world.entities.begin(),home.world.entities.end(),[](const auto& e){return e.id==2;});require(found!=home.world.entities.end()&&found->person_data[10]==777&&home.household_roster.size()==3,"non-HHRD destination writer returns to unchanged HOME roster/body");
 auto mismatch=envelope;mismatch.personal_state.reset();rejects([&]{odh::encode_session_bundle(mismatch);});
 std::cout<<"visited NPC/HOME carrier projection and envelope mismatch passed\n";
}
void matrix_wardrobe_owner(){
 auto n=seed(10,false);odh::NativePersonalState owner(n,digest());const auto legacy=odh::encode_native_personal_snapshot(owner.snapshot());
 odh::OriginalWardrobeSource raw;raw.body_str_id=200;raw.strings.resize(35);
 raw.strings[0]="adult";raw.strings[1]="b004mafit_01,BODY=b004mafitlgt_rat";
 raw.strings[12]="male";raw.strings[13]="27";raw.strings[14]="lgt";
 raw.strings[30]="f100mafit_original,BODY=f100mafitlgt_original";
 raw.strings[31]="s100mafit_original,BODY=s100mafitlgt_original";
 raw.strings[32]="l100mafit_original,BODY=l100mafitlgt_original";
 raw.strings[33]="w100mafit_original,BODY=w100mafitlgt_original";
 raw.strings[34]="h533mafit_zoot,BODY=h533mafitlgt_zoot";
 const std::vector<odh::NativeWardrobeSource> sources{{identity(),raw},{identity(42),raw}};
 // A uniform-only source is valid for read/load but its explicit upgrade still needs catalog authority.
 auto uniform=raw;uniform.strings[1]="NewsieFChd_01,BODY=NewsieFChd_01";
 uniform.strings[12]="female";uniform.strings[13]="10";uniform.strings[14]="white";
 for(std::size_t i=30;i<35;++i)uniform.strings[i]="";
 const odh::NativeWardrobeSource absent{identity(43),uniform};
 require(!owner.verify_wardrobe_source(absent)&&odh::encode_native_personal_snapshot(owner.snapshot())==legacy,
     "fresh read preserves absent wardrobe and exact legacy bytes without catalog production");
 rejects([&]{owner.prepare_wardrobes({absent},{});});
 odh::NativePersonalState retired_owner(n,digest(),{43});
 rejects([&]{retired_owner.verify_wardrobe_source(absent);});
 auto wrong=absent;wrong.identity.guid^=1;rejects([&]{owner.verify_wardrobe_source(wrong);});
 auto missing=absent;missing.identity=identity(44);rejects([&]{owner.verify_wardrobe_source(missing);});
 auto p=owner.prepare_wardrobes(sources,{});require(owner.snapshot().version==1&&owner.snapshot().records.empty()&&owner.snapshot().wardrobes.empty(),"wardrobe preview never fabricates schema0 numeric data or mutates owner");
 require(owner.commit(std::move(p))&&!owner.commit(std::move(p)),"wardrobe admission commits once at same-owner safe point");
 require(owner.changed()&&owner.snapshot().version==3&&owner.snapshot().records.empty()&&owner.snapshot().wardrobes.size()==2,"wardrobe-only v3 bank retains independent numeric absence");
 const auto wire=odh::encode_native_personal_snapshot(owner.snapshot());const auto decoded=odh::decode_native_personal_snapshot(wire);
 require(odh::encode_native_personal_snapshot(decoded)==wire,"v3 wardrobe exact codec roundtrip");
 odh::NativePersonalState restored(n,digest());restored.restore(decoded);require(restored.wardrobe_by_id(41)&&!restored.wardrobe_by_id(43),"cold owner restores wardrobe without invented numeric records");
 require(!restored.verify_wardrobe_source(absent)&&odh::encode_native_personal_snapshot(restored.snapshot())==wire,
     "cold read preserves missing uniform wardrobe and every accepted record byte");
 require(restored.verify_wardrobe_source(sources.front())==restored.wardrobe_by_id(41),
     "accepted read returns the existing exact record without resampling");
 auto rebound=sources.front();rebound.identity.character_fingerprint[0]^=1;
 rejects([&]{restored.verify_wardrobe_source(rebound);});
 auto tampered=sources.front();tampered.source.strings[14]="med";
 rejects([&]{restored.verify_wardrobe_source(tampered);});
 auto idempotent=restored.prepare_wardrobes(sources,{});require(odh::encode_native_personal_snapshot(idempotent.preview_snapshot())==wire,"existing accepted wardrobe is not resampled or revision-bumped");
 require(!restored.verify_wardrobe_source(absent)&&restored.source_current(idempotent),
     "read-only absence does not invalidate a prepared producer epoch");
 auto mixed=sources;mixed.push_back(absent);rejects([&]{restored.prepare_wardrobes(mixed,{});});
 require(odh::encode_native_personal_snapshot(restored.snapshot())==wire&&restored.source_current(idempotent),
     "failed explicit uniform upgrade preserves accepted records and pending epochs");
 odh::World world;person(world);auto numeric=restored.prepare(world,{source(world)},odh::NativePersonalWriteSource::generic25);
 require(restored.commit(std::move(numeric),world)&&!restored.commit(std::move(idempotent)),"actual numeric commit invalidates pending wardrobe through same epoch");
 require(restored.snapshot().wardrobes.size()==2&&restored.snapshot().records.size()==1,"numeric writer retains all independent wardrobe records");
 auto concurrent=restored.prepare(world,{source(world)},odh::NativePersonalWriteSource::generic25);auto wardrobe=restored.prepare_wardrobes(sources,{});
 require(restored.commit(std::move(wardrobe))&&!restored.commit(std::move(concurrent),world),"wardrobe commit invalidates prepared numeric writer atomically");
 for(unsigned mode=0;mode<5;++mode){auto bad=decoded;
  if(mode==0)bad.wardrobes.pop_back();
  if(mode==1)--bad.wardrobes.front().revision;
  if(mode==2)bad.wardrobes.front().identity.character_fingerprint[0]^=1;
  if(mode==3)bad.wardrobes.front().wardrobe.seed[3]="changed";
  if(mode==4)bad.wardrobes.front().wardrobe.strings[3]="w200mafit_custom,BODY=w200mafitlgt_custom";
  rejects([&]{odh::validate_native_personal_successor(decoded,bad);});
 }
 auto changed=sources;changed.front().source.strings[14]="med";rejects([&]{restored.prepare_wardrobes(changed,{});});
 const auto before=odh::encode_native_personal_snapshot(restored.snapshot());
 auto duplicate=sources;duplicate.push_back(sources.front());rejects([&]{restored.prepare_wardrobes(duplicate,{});});
 require(odh::encode_native_personal_snapshot(restored.snapshot())==before,"failed grouped wardrobe admission is atomic");
 for(std::size_t cut=0;cut<wire.size();++cut){auto truncated=wire;truncated.resize(cut);rejects([&]{odh::decode_native_personal_snapshot(truncated);});}
 auto old=odh::decode_native_personal_snapshot(legacy);require(old.version==1&&odh::encode_native_personal_snapshot(old)==legacy,"absent wardrobe keeps legacy v1 bytes exact");
 std::cout<<"wardrobe-only v3 owner/epoch/codec/antirollback matrix passed\n";
}

}
int main(){try{matrix_wardrobe_owner();matrix_resource_locators();matrix_packaged_retirement();matrix_records();matrix_atomicity();matrix_schema();matrix_shared();matrix_sessions();matrix_travel();std::cout<<checks<<" checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
