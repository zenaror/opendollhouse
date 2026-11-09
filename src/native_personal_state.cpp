// SPDX-License-Identifier: MPL-2.0
#include "odh/native_personal_state.hpp"
#include "odh/ts1_character_family.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace odh {
namespace {
struct Field {std::size_t index;unsigned minimum;};
constexpr Field fields[]={{2,1},{3,1},{4,1},{5,1},{6,1},{7,1},{9,1},{10,1},{11,1},{12,1},
 {16,1},{20,1},{15,1},{26,1},{17,1},{18,1},{46,1},{47,1},{48,1},{49,1},{50,1},{51,1},
 {52,1},{53,1},{13,1},{14,1},{58,1},{60,1},{61,1},{56,2},{57,2},{63,2},{65,3},
 {67,4},{68,4},{69,5},{74,6},{54,7},{55,7},{75,7},{76,7},{80,1},{81,1},{82,1}};
bool nonzero(const std::array<std::uint8_t,32>& hash){return std::any_of(hash.begin(),hash.end(),[](auto v){return v!=0;});}
void portable_resource_name(const std::string& value,bool entry){
 if(value.empty()||value.size()>4096||value.find('\0')!=std::string::npos||
    value.find(':')!=std::string::npos||value.front()=='/'||value.front()=='\\'||
    (!entry&&value.find('\\')!=std::string::npos))throw FormatError("personal resource name is not portable");
 std::size_t start=0;
 for(std::size_t at=0;at<=value.size();++at)if(at==value.size()||value[at]=='/'||(entry&&value[at]=='\\')){
  const auto part=value.substr(start,at-start);
  if(part.empty()||part=="."||part=="..")throw FormatError("personal resource name contains noncanonical components");
  start=at+1;
 }
}
bool character_source(const std::string& source){
 const auto slash=source.find('/');
 if(slash==std::string::npos||source.substr(slash,12)!="/Characters/")return false;
 const auto root=source.substr(0,slash);
 const bool installed=root=="UserData"||(root.size()==9&&root.compare(0,8,"UserData")==0&&root[8]>='1'&&root[8]<='8');
 return (installed||root=="PlayerData")&&source.find('/',slash+12)==std::string::npos;
}
std::string hex_bytes(const std::string& value){
 constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(value.size()*2);
 for(const unsigned char byte:value){out+=hex[byte>>4];out+=hex[byte&15];}return out;
}
std::string unhex_bytes(const std::string& value){
 if(value.size()%2)throw FormatError("personal resource hex length is invalid");
 const auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return static_cast<unsigned>(c-'0');if(c>='a'&&c<='f')return static_cast<unsigned>(c-'a'+10);throw FormatError("personal resource hex is not canonical");};
 std::string out;out.reserve(value.size()/2);
 for(std::size_t at=0;at<value.size();at+=2)out+=static_cast<char>((digit(value[at])<<4)|digit(value[at+1]));return out;
}
void identity(const NativePersonalIdentity& id){
 if(id.neighbor_id<=0||!id.guid||id.character_asset.empty()||id.character_asset.size()>4096||
    id.character_asset.find('\0')!=std::string::npos||!nonzero(id.character_fingerprint))
  throw FormatError("personal record has incomplete character provenance");
 (void)native_personal_resource_locator(id.character_asset);
}
void projection(const NativePersonalProjection& p){
 if((p.profile!=1&&p.profile!=2)||(p.profile==1&&p.schema_version<1)||p.schema_version>9||(p.profile==2&&!p.present[61]))throw FormatError("personal record schema/profile unsupported");
 for(std::size_t i=0;i<p.present.size();++i){
  const auto field=std::find_if(std::begin(fields),std::end(fields),[&](const auto& f){return f.index==i;});
  if(p.present[i]>1||(p.present[i]&&(field==std::end(fields)||(field->minimum>p.schema_version&&!(p.profile==2&&(i==61||i==67))))))
   throw FormatError("personal record field presence violates selective schema");
  if(!p.present[i]&&p.values[i]!=0)throw FormatError("absent personal field carries a value");
 }
}
const Entity& source(const World& world,const NativePersonalSource& s){
 const auto* e=world.find(s.body.id);
 if(!e||e->generation!=s.body.generation||e->kind!=EntityKind::avatar||e->ui_ghost||
    !e->definition||e->definition->guid!=s.identity.guid||e->person_data.size()<=82||
    e->person_data[31]!=s.identity.neighbor_id)
  throw FormatError("personal writer source body no longer matches authenticated identity");
 return *e;
}
std::int16_t signed16(std::uint16_t v){return static_cast<std::int16_t>(v<32768?v:static_cast<std::int32_t>(v)-65536);}
}
std::string native_personal_resource_key(const std::string& source,const std::string& asset){
 portable_resource_name(source,false);
 if(!asset.empty())portable_resource_name(asset,true);
 if(asset.empty()&&character_source(source)){
  const auto name=source.substr(source.find_last_of('/')+1);
  return std::string(source.compare(0,11,"PlayerData/")==0?"PlayerData/Characters/":"UserData/Characters/")+name;
 }
 auto key=std::string("Resource/v1/")+hex_bytes(source)+"/"+hex_bytes(asset);
 if(key.size()>4096)throw FormatError("personal resource locator exceeds bounded identity size");
 return key;
}
NativePersonalResourceLocator native_personal_resource_locator(const std::string& key){
 if(key.empty()||key.size()>4096)throw FormatError("personal resource locator size is invalid");
 NativePersonalResourceLocator out;
 if(key.compare(0,20,"UserData/Characters/")==0||key.compare(0,22,"PlayerData/Characters/")==0){
  portable_resource_name(key,false);if(!character_source(key))throw FormatError("personal character locator is not canonical");
  out.source=key;out.character_catalog=true;
 }else{
  constexpr std::size_t prefix=12;
  if(key.compare(0,prefix,"Resource/v1/")!=0)throw FormatError("personal resource locator kind unsupported");
  const auto slash=key.find('/',prefix);
  if(slash==std::string::npos||key.find('/',slash+1)!=std::string::npos)throw FormatError("personal resource locator fields malformed");
  out.source=unhex_bytes(key.substr(prefix,slash-prefix));out.asset=unhex_bytes(key.substr(slash+1));
 }
 if(native_personal_resource_key(out.source,out.asset)!=key)throw FormatError("personal resource locator has an alternate encoding");
 return out;
}
bool same_native_personal_identity(const NativePersonalIdentity& a,const NativePersonalIdentity& b){
 return a.neighbor_id==b.neighbor_id&&a.guid==b.guid&&a.definition_chunk_id==b.definition_chunk_id&&
 a.character_asset==b.character_asset&&a.character_fingerprint==b.character_fingerprint;
}
bool native_personal_v9_field(std::size_t index) noexcept {
 return std::any_of(std::begin(fields),std::end(fields),[&](const auto& f){return f.index==index;});
}
std::optional<NativePersonalProjection> original_neighbor_seed_projection(const NeighborRecord& n){
 if((n.version!=4&&n.version!=10)||n.person_mode<0||n.person_mode>9||
    (n.person_mode>0)!=n.person_data.has_value()||
    (n.person_data&&n.person_data->size()!=(n.version==4?80U:256U)))throw FormatError("original personal seed schema/shape unsupported");
 if(!n.person_mode)return {};
 NativePersonalProjection p;p.schema_version=static_cast<std::uint8_t>(n.person_mode);
 for(const auto f:fields)if(f.minimum<=static_cast<unsigned>(n.person_mode)){
  p.present[f.index]=1;p.values[f.index]=f.index<n.person_data->size()?n.person_data->at(f.index):0;
 }
 return p;
}
void apply_original_neighbor_personal(const NeighborRecord& n,std::vector<std::int16_t>& bank){
 if(bank.size()!=256||n.neighbor_id<=0||!n.guid)throw FormatError("original personal binding needs the native256-word target and exact neighbor");
 const auto source=original_neighbor_seed_projection(n);
 if(source)apply_native_personal_projection(*source,bank);
 // Family is bound separately even for schema0, whose original record storage
 // was initialized to zero. It is not permission to populate other absent fields.
 bank[31]=n.neighbor_id;bank[61]=n.person_data?n.person_data->at(61):0;
 if(n.native_personal)apply_native_personal_projection(*n.native_personal,bank);
}
void apply_native_personal_projection(const NativePersonalProjection& p,std::vector<std::int16_t>& bank){
 projection(p);
 for(const auto f:fields)if((f.minimum<=p.schema_version||(p.profile==2&&(f.index==61||f.index==67)))&&p.present[f.index]){
  if(f.index>=bank.size())throw FormatError("personal loader target bank lacks a present field");
 }
 for(const auto f:fields)if((f.minimum<=p.schema_version||(p.profile==2&&(f.index==61||f.index==67)))&&p.present[f.index])bank[f.index]=p.values[f.index];
}
void validate_native_personal_snapshot(const NativePersonalSnapshot& s){
 if((s.version!=1&&s.version!=2&&s.version!=3&&s.version!=4)||(s.version==1&&s.catalog)||(s.version==2&&!s.catalog)||(s.version<3&&!s.wardrobes.empty())||(s.version==3&&s.wardrobes.empty())||!nonzero(s.neighborhood_identity)||s.records.size()>4096||s.wardrobes.size()>4096)throw FormatError("personal snapshot header unsupported");
 if(s.catalog){validate_native_personal_catalog(*s.catalog);if(s.catalog->neighborhood_identity!=s.neighborhood_identity)throw FormatError("personal catalog neighborhood differs");}
 std::int16_t previous=0;
 for(const auto& r:s.records){identity(r.identity);projection(r.projection);
  if(r.projection.profile==2&&(s.version!=4||r.writer!=NativePersonalWriteSource::family_membership))throw FormatError("explicit family-write field requires NPER4 membership provenance");
  if(r.identity.neighbor_id<=previous||!r.revision||static_cast<unsigned>(r.writer)<1||static_cast<unsigned>(r.writer)>(s.version==4?5U:4U))
   throw FormatError("personal record order/revision/writer invalid");
  previous=r.identity.neighbor_id;
  if(s.catalog)for(std::size_t i=0;i<s.catalog->bindings.size();++i)if(s.catalog->bindings[i].neighbor_id==r.identity.neighbor_id){const auto& d=s.catalog->definitions[i];
   if(d.guid!=r.identity.guid||d.definition_chunk_id!=r.identity.definition_chunk_id||d.fingerprint!=r.identity.character_fingerprint||native_personal_resource_key(d.source,d.asset)!=r.identity.character_asset)throw FormatError("personal record differs from its owned hydrated resource binding");
  }
  if(r.seed_words&&r.seed_words->size()!=80&&r.seed_words->size()!=256)throw FormatError("personal seed bank shape unsupported");
 }
 std::size_t wardrobe_bytes=0;
 previous=0;
 for(const auto& r:s.wardrobes){
  identity(r.identity);validate_original_wardrobe_projection(r.wardrobe);
  const auto account=[&](std::size_t n){if(n>24u*1024u*1024u-wardrobe_bytes)throw FormatError("wardrobe collection exceeds bounded size");wardrobe_bytes+=n;};
  account(56+r.identity.character_asset.size());
  const auto text=[&](const std::string& v){account(2+v.size());};
  const auto optional_text=[&](const std::optional<std::string>& v){account(1);if(v)text(*v);};
  for(const auto& v:r.wardrobe.source_fields)optional_text(v);for(const auto& v:r.wardrobe.seed)optional_text(v);for(const auto& v:r.wardrobe.strings)optional_text(v);
  for(const auto& v:r.wardrobe.chosen){account(1);if(v){text(v->literal);for(const auto* a:{&v->appearance,&v->texture}){account(41);text(a->name);text(a->source);text(a->asset);}}}

  if(r.identity.neighbor_id<=previous||!r.revision)throw FormatError("wardrobe record order/revision invalid");
  previous=r.identity.neighbor_id;
  const auto numeric=std::lower_bound(s.records.begin(),s.records.end(),r.identity.neighbor_id,
      [](const auto& record,auto nid){return record.identity.neighbor_id<nid;});
  if(numeric!=s.records.end()&&numeric->identity.neighbor_id==r.identity.neighbor_id&&
     !same_native_personal_identity(numeric->identity,r.identity))throw FormatError("wardrobe and numeric identity differ");
  if(s.catalog)for(std::size_t i=0;i<s.catalog->bindings.size();++i)if(s.catalog->bindings[i].neighbor_id==r.identity.neighbor_id){const auto& d=s.catalog->definitions[i];
   if(d.guid!=r.identity.guid||d.definition_chunk_id!=r.identity.definition_chunk_id||d.fingerprint!=r.identity.character_fingerprint||native_personal_resource_key(d.source,d.asset)!=r.identity.character_asset)throw FormatError("wardrobe differs from admitted hydrated resource binding");
  }
 }

}
void validate_native_personal_successor(const NativePersonalSnapshot& before,const NativePersonalSnapshot& after){
 validate_native_personal_snapshot(before);validate_native_personal_snapshot(after);
 if(before.catalog&&(!after.catalog||encode_native_personal_catalog(*before.catalog)!=encode_native_personal_catalog(*after.catalog)))throw FormatError("personal successor changes or removes its admitted hydrated catalog");
 if(before.neighborhood_identity!=after.neighborhood_identity)throw FormatError("personal successor neighborhood changed");
 if(before.version>=3&&after.version<before.version)throw FormatError("personal successor removes wardrobe profile");
 for(const auto& old:before.wardrobes){
  const auto next=std::lower_bound(after.wardrobes.begin(),after.wardrobes.end(),old.identity.neighbor_id,
      [](const auto& r,auto nid){return r.identity.neighbor_id<nid;});
  if(next==after.wardrobes.end()||next->identity.neighbor_id!=old.identity.neighbor_id||
     !same_native_personal_identity(old.identity,next->identity)||next->revision<old.revision)
   throw FormatError("personal successor drops, rolls back or rebinds wardrobe");
  auto a=before,b=after;a.records.clear();b.records.clear();a.catalog.reset();b.catalog.reset();
  a.wardrobes={old};b.wardrobes={*next};b.wardrobes.front().revision=old.revision;a.version=b.version=3;
  if(encode_native_personal_snapshot(a)!=encode_native_personal_snapshot(b))
   throw FormatError("personal successor changes accepted wardrobe seed or choice");
 }

 for(const auto& old:before.records){
  const auto next=std::lower_bound(after.records.begin(),after.records.end(),old.identity.neighbor_id,
      [](const auto& r,auto nid){return r.identity.neighbor_id<nid;});
  if(next==after.records.end()||next->identity.neighbor_id!=old.identity.neighbor_id||
     !same_native_personal_identity(old.identity,next->identity)||next->seed_words!=old.seed_words||next->revision<old.revision)
   throw FormatError("personal successor drops, rolls back or changes an authenticated record");
  if(next->revision==old.revision){
   NativePersonalSnapshot a{4,before.neighborhood_identity,{old}},b{4,after.neighborhood_identity,{*next}};
   if(encode_native_personal_snapshot(a)!=encode_native_personal_snapshot(b))throw FormatError("personal successor changes a value without a writer revision");
  }
 }
}
NativePersonalState::NativePersonalState(const Nbrs& seed,const std::array<std::uint8_t,32>& digest,
 std::set<std::int16_t> retired,const NativePersonalCatalog* catalog,const std::vector<NeighborRecord>& owned):retired_(std::move(retired)){
 state_.neighborhood_identity=digest;
 if(catalog){if(catalog->neighborhood_identity!=digest)throw FormatError("personal catalog source digest differs");state_.version=2;state_.catalog=*catalog;}
 validate_native_personal_snapshot(state_);
 const auto owned_seed=catalog?hydrate_owned_native_personal_neighbors(seed,*catalog,owned,retired_):
    (owned.empty()?seed:merge_owned_neighbor_seeds(seed,owned));
 if(owned_seed.entries.size()>4096)throw FormatError("personal seed exceeds neighbor count limit");
 for(const auto& entry:owned_seed.entries){
  if(entry.marker==0&&!entry.record)continue;
  if(entry.marker!=1||!entry.record)throw FormatError("personal seed marker inconsistent");
  const auto& r=*entry.record;
  if((r.version!=4&&r.version!=10)||(r.person_mode>0)!=r.person_data.has_value()||
     (r.person_data&&r.person_data->size()!=(r.version==4?80u:256u))||r.native_personal||
     !seeds_.emplace(r.neighbor_id,r).second)throw FormatError("personal seed record invalid or duplicate");
 }
}
std::map<std::int16_t,NeighborRecord> NativePersonalState::materialize(const NativePersonalSnapshot& s) const {
 validate_native_personal_snapshot(s);
 if((s.version!=3&&s.version!=4&&s.version!=state_.version)||bool(s.catalog)!=bool(state_.catalog)||(s.catalog&&encode_native_personal_catalog(*s.catalog)!=encode_native_personal_catalog(*state_.catalog)))throw FormatError("personal restore catalog was not admitted before seed construction");
 if(s.neighborhood_identity!=state_.neighborhood_identity)throw FormatError("personal snapshot neighborhood differs");
 for(const auto& r:s.wardrobes){const auto seed=seeds_.find(r.identity.neighbor_id);
  if(seed==seeds_.end()&&retired_.count(r.identity.neighbor_id))continue;
  if(seed==seeds_.end()||seed->second.guid!=r.identity.guid)throw FormatError("wardrobe immutable NBRS identity differs");
 }
 std::map<std::int16_t,NeighborRecord> out;
 for(const auto& r:s.records){const auto seed=seeds_.find(r.identity.neighbor_id);
  // Explicit family deletion/archive tombstones retain historical records;
  // they grant neither construction nor live neighbor lookup authority.
  if(seed==seeds_.end()&&retired_.count(r.identity.neighbor_id))continue;
  if(seed==seeds_.end()||seed->second.guid!=r.identity.guid||seed->second.person_data!=r.seed_words)
   throw FormatError("personal snapshot immutable NBRS identity/bank differs");
  auto record=seed->second;record.native_personal=r.projection;out.emplace(r.identity.neighbor_id,std::move(record));
 }
 return out;
}
const NeighborRecord* NativePersonalState::by_id(std::int16_t nid) const {
 const auto i=effective_.find(nid);return i==effective_.end()?nullptr:&i->second;
}
void validate_ts1_created_character(const Ts1CreatedCharacter& c){
 const auto& id=c.identity;const auto& n=c.seed;
 if(id.neighbor_id<=0||!id.guid||id.character_asset.empty()||id.character_asset.size()>4096||!nonzero(id.character_fingerprint)||
    id.neighbor_id!=n.neighbor_id||id.guid!=n.guid||n.native_personal||n.version!=10||n.unknown3!=std::optional<std::int32_t>{0}||
    n.name.empty()||n.name.size()>256||n.name.find('\0')!=std::string::npos||n.mystery_zero||n.person_mode!=0||n.person_data||n.unknown_neg_one!=-1||!n.relationships.empty()||
    (n.name.size()%2==0?n.name_padding!=std::optional<std::uint8_t>{0}:n.name_padding.has_value()))throw FormatError("created character differs from exact original schema0 resource/neighbor seed");
 const auto loc=native_personal_resource_locator(id.character_asset);
 if(!loc.character_catalog||loc.source.compare(0,22,"PlayerData/Characters/")!=0)throw FormatError("created character must name owned private character data");
 if(n.name!=loc.source.substr(loc.source.find_last_of('/')+1))throw FormatError("created original neighbor name differs from private filename");
}
void NativePersonalState::admit_created_character(const Ts1CreatedCharacter& c){
 validate_ts1_created_character(c);const auto nid=c.identity.neighbor_id;
 const auto old=seeds_.find(nid);
 if(old!=seeds_.end()){
  const auto same=[](const NeighborRecord& a,const NeighborRecord& b){return encode_nbrs(Nbrs{0,73,{{1,a}},0})==encode_nbrs(Nbrs{0,73,{{1,b}},0});};
  if(!same(old->second,c.seed)||retired_.count(nid))throw FormatError("created personal seed conflicts with source");
  const auto record=std::lower_bound(state_.records.begin(),state_.records.end(),nid,[](const auto& r,auto id){return r.identity.neighbor_id<id;});
  if(record!=state_.records.end()&&record->identity.neighbor_id==nid){if(!same_native_personal_identity(record->identity,c.identity))throw FormatError("created personal identity changed");return;}
  if(state_.version==4)return; // Exact seed-only admission is already complete.
 }else if(seeds_.size()>=4096)throw FormatError("created personal seed count exceeds budget");
 if(epoch_==UINT64_MAX||state_.records.size()>=4096)throw FormatError("created personal revision/count exceeded");
 auto candidate=state_;candidate.version=4;validate_native_personal_snapshot(candidate);
 if(epoch_==UINT64_MAX)throw FormatError("created personal seed epoch exhausted");
 seeds_.emplace(nid,c.seed);state_=std::move(candidate);++epoch_;
}
void NativePersonalState::set_family(const NativePersonalIdentity& id,std::int16_t family,bool clear_lingering_house){
 identity(id);const auto seed=seeds_.find(id.neighbor_id);
 if(seed==seeds_.end()||retired_.count(id.neighbor_id)||seed->second.guid!=id.guid||(seed->second.person_data&&seed->second.person_data->size()<=67)||epoch_==UINT64_MAX)
  throw FormatError("family write lacks exact existing neighbor person bank");
 auto candidate=state_;candidate.version=4;auto where=std::lower_bound(candidate.records.begin(),candidate.records.end(),id.neighbor_id,[](const auto& r,auto nid){return r.identity.neighbor_id<nid;});
 if(where==candidate.records.end()||where->identity.neighbor_id!=id.neighbor_id){
  if(candidate.records.size()>=4096)throw FormatError("family personal record budget exceeded");auto projection=original_neighbor_seed_projection(seed->second);
  if(!projection){projection=NativePersonalProjection{};projection->profile=2;projection->schema_version=0;}
  where=candidate.records.insert(where,{id,1,NativePersonalWriteSource::family_membership,*projection,seed->second.person_data});
 }else{
  if(!same_native_personal_identity(where->identity,id)||where->revision==UINT64_MAX)throw FormatError("family neighbor resource identity/revision changed");++where->revision;where->writer=NativePersonalWriteSource::family_membership;
 }
 where->projection.present[61]=1;where->projection.values[61]=family;
 if(clear_lingering_house){where->projection.profile=2;where->projection.present[67]=1;where->projection.values[67]=0;}
 validate_native_personal_snapshot(candidate);auto effective=materialize(candidate);state_=std::move(candidate);effective_.swap(effective);++epoch_;
}
void NativePersonalState::restore(const NativePersonalSnapshot& s){
 if(changed())validate_native_personal_successor(state_,s);
 auto effective=materialize(s);auto candidate=s;
 if(epoch_==std::numeric_limits<std::uint64_t>::max())throw FormatError("personal epoch exhausted");
 state_=std::move(candidate);effective_=std::move(effective);++epoch_;
}
const NativeWardrobeRecord* NativePersonalState::wardrobe_by_id(std::int16_t nid) const {
 const auto found=std::lower_bound(state_.wardrobes.begin(),state_.wardrobes.end(),nid,
     [](const auto& r,auto key){return r.identity.neighbor_id<key;});
 return found!=state_.wardrobes.end()&&found->identity.neighbor_id==nid?&*found:nullptr;
}
PreparedNativeWardrobeCommit NativePersonalState::prepare_wardrobes(const std::vector<NativeWardrobeSource>& input,
 const std::vector<WardrobeCatalogAsset>& catalog) const {
 if(input.empty()||input.size()>4096||epoch_==std::numeric_limits<std::uint64_t>::max())
  throw FormatError("wardrobe admission requires bounded explicit sources and live epoch");
 PreparedNativeWardrobeCommit out;out.owner_=this;out.epoch_=epoch_;out.candidate_=state_;
 std::set<std::int16_t> unique;
 for(const auto& item:input){identity(item.identity);
  if(!unique.insert(item.identity.neighbor_id).second)throw FormatError("wardrobe admission repeats neighbor");
  const auto seed=seeds_.find(item.identity.neighbor_id);
  if(seed==seeds_.end()||retired_.count(item.identity.neighbor_id)||seed->second.guid!=item.identity.guid)
   throw FormatError("wardrobe admission lacks exact active NBRS identity");
  auto found=std::lower_bound(out.candidate_.wardrobes.begin(),out.candidate_.wardrobes.end(),item.identity.neighbor_id,
      [](const auto& r,auto nid){return r.identity.neighbor_id<nid;});
  if(found!=out.candidate_.wardrobes.end()&&found->identity.neighbor_id==item.identity.neighbor_id){
   if(!same_native_personal_identity(found->identity,item.identity))throw FormatError("wardrobe admission resource changed");
   validate_original_wardrobe_source(found->wardrobe,item.source);
  }else{
   NativeWardrobeRecord fresh;fresh.identity=item.identity;
   fresh.wardrobe=prepare_original_wardrobe_upgrade(item.source,catalog);
   out.candidate_.wardrobes.insert(found,std::move(fresh));
  }
 }
 out.candidate_.version=std::max(out.candidate_.version,3U);validate_native_personal_successor(state_,out.candidate_);
 (void)materialize(out.candidate_);(void)encode_native_personal_snapshot(out.candidate_);return out;
}
bool NativePersonalState::source_current(const PreparedNativeWardrobeCommit& p) const {
 return p.owner_==this&&p.epoch_==epoch_&&!p.candidate_.wardrobes.empty()&&epoch_!=std::numeric_limits<std::uint64_t>::max();
}
bool NativePersonalState::commit(PreparedNativeWardrobeCommit&& p){
 if(!source_current(p))return false;
 state_=std::move(p.candidate_);++epoch_;p.owner_=nullptr;return true;
}
PreparedNativePersonalCommit NativePersonalState::prepare(const World& world,const std::vector<NativePersonalSource>& input,NativePersonalWriteSource writer) const {
 if(static_cast<unsigned>(writer)<1||static_cast<unsigned>(writer)>4)throw FormatError("personal writer source is unsupported");
 if(input.empty()||input.size()>4096)throw FormatError("personal writer requires a bounded explicit source list");
 if(epoch_==std::numeric_limits<std::uint64_t>::max())throw FormatError("personal epoch exhausted");
 PreparedNativePersonalCommit out;out.owner_=this;out.epoch_=epoch_;out.candidate_=state_;out.sources_=input;
 std::set<std::int16_t> unique;
 for(const auto& item:input){identity(item.identity);
  if(!unique.insert(item.identity.neighbor_id).second)throw FormatError("personal writer repeats a neighbor");
  const auto& body=source(world,item);out.expected_.push_back(body.person_data);
  const auto seed=seeds_.find(item.identity.neighbor_id);
  if(seed==seeds_.end()||seed->second.guid!=item.identity.guid)throw FormatError("personal writer lacks authenticated NBRS seed");
  auto record=std::lower_bound(out.candidate_.records.begin(),out.candidate_.records.end(),item.identity.neighbor_id,
      [](const auto& r,auto nid){return r.identity.neighbor_id<nid;});
  if(record==out.candidate_.records.end()||record->identity.neighbor_id!=item.identity.neighbor_id){
   NativePersonalRecord fresh;fresh.identity=item.identity;fresh.seed_words=seed->second.person_data;
   record=out.candidate_.records.insert(record,std::move(fresh));
  }else if(!same_native_personal_identity(record->identity,item.identity))throw FormatError("personal writer character provenance changed");
  if(record->revision==std::numeric_limits<std::uint64_t>::max())throw FormatError("personal revision exhausted");
  ++record->revision;record->writer=writer;record->projection={};
  for(const auto f:fields){record->projection.present[f.index]=1;record->projection.values[f.index]=body.person_data[f.index];}
 }
 out.effective_=materialize(out.candidate_);return out;
}
bool NativePersonalState::source_current(const PreparedNativePersonalCommit& p,const World& world) const {
 if(p.owner_!=this||p.epoch_!=epoch_||p.sources_.empty()||p.sources_.size()!=p.expected_.size())return false;
 try{for(std::size_t i=0;i<p.sources_.size();++i)if(source(world,p.sources_[i]).person_data!=p.expected_[i])return false;}catch(const FormatError&){return false;}
 return true;
}
bool NativePersonalState::publish(PreparedNativePersonalCommit&& p){
 if(p.owner_!=this||p.epoch_!=epoch_||p.sources_.empty())return false;
 state_.records.swap(p.candidate_.records);effective_.swap(p.effective_);++epoch_;p.owner_=nullptr;return true;
}
bool NativePersonalState::commit(PreparedNativePersonalCommit&& p,const World& world){
 return source_current(p,world)&&publish(std::move(p));
}
bool NativePersonalState::commit_retirement(PreparedNativePersonalCommit&& p,World& world,const std::function<bool()>& retire){
 if(!retire||!source_current(p,world)||!retire())return false;
 if(p.owner_!=this||p.epoch_!=epoch_)return false;
 for(std::size_t i=0;i<p.sources_.size();++i){const auto& item=p.sources_[i];
  const auto* e=world.find(item.body.id);if(e&&e->generation==item.body.generation)return false;
  const auto* retained=world.resolve_reference(item.body);
  if(retained&&retained->person_data!=p.expected_[i])return false;
 }
 return publish(std::move(p));
}
std::vector<std::uint8_t> encode_native_personal_snapshot(const NativePersonalSnapshot& s){
 validate_native_personal_snapshot(s);std::vector<std::uint8_t> b;
 const auto put=[&](std::uint64_t v,unsigned n){if(n>24u*1024u*1024u-b.size())throw FormatError("personal snapshot exceeds bounded size");for(unsigned i=0;i<n;++i)b.push_back(static_cast<std::uint8_t>(v>>(8*i)));};
 const auto text=[&](const std::string& v){if(v.size()>4096||v.find('\0')!=std::string::npos)throw FormatError("wardrobe text outside bound");put(v.size(),2);if(v.size()>24u*1024u*1024u-b.size())throw FormatError("personal snapshot exceeds bounded size");b.insert(b.end(),v.begin(),v.end());};
 const auto optional_text=[&](const std::optional<std::string>& v){put(v.has_value(),1);if(v)text(*v);};
 const auto asset=[&](const WardrobeCatalogAsset& a){put(static_cast<unsigned>(a.kind),1);text(a.name);text(a.source);text(a.asset);b.insert(b.end(),a.fingerprint.begin(),a.fingerprint.end());put(a.producer_ordinal,8);};
 put(s.version,4);b.insert(b.end(),s.neighborhood_identity.begin(),s.neighborhood_identity.end());put(s.records.size(),4);
 for(const auto& r:s.records){const auto& id=r.identity;put(static_cast<std::uint16_t>(id.neighbor_id),2);put(id.guid,4);put(id.definition_chunk_id,2);
  put(id.character_asset.size(),2);b.insert(b.end(),id.character_asset.begin(),id.character_asset.end());
  b.insert(b.end(),id.character_fingerprint.begin(),id.character_fingerprint.end());put(r.revision,8);put(static_cast<unsigned>(r.writer),1);
  put(r.projection.profile,1);put(r.projection.schema_version,1);put(r.seed_words?r.seed_words->size():0,2);
  if(r.seed_words)for(auto word:*r.seed_words)put(static_cast<std::uint16_t>(word),2);
  for(std::size_t i=0;i<r.projection.present.size();++i){put(r.projection.present[i],1);if(r.projection.present[i])put(static_cast<std::uint16_t>(r.projection.values[i]),2);}
 }
 if(s.version>=2&&s.version<=4){const auto catalog=s.catalog?encode_native_personal_catalog(*s.catalog):std::vector<std::uint8_t>{};put(catalog.size(),4);b.insert(b.end(),catalog.begin(),catalog.end());}
 if(s.version>=3&&s.version<=4){put(s.wardrobes.size(),4);for(const auto& r:s.wardrobes){const auto& id=r.identity;
  put(static_cast<std::uint16_t>(id.neighbor_id),2);put(id.guid,4);put(id.definition_chunk_id,2);text(id.character_asset);
  b.insert(b.end(),id.character_fingerprint.begin(),id.character_fingerprint.end());put(r.revision,8);
  const auto& w=r.wardrobe;put(w.rule_profile,4);put(w.body_str_id,2);
  for(const auto& v:w.source_fields)optional_text(v);for(const auto& v:w.seed)optional_text(v);for(const auto& v:w.strings)optional_text(v);
  for(const auto& v:w.chosen){put(v.has_value(),1);if(v){text(v->literal);asset(v->appearance);asset(v->texture);}}
 }}
 if(b.size()>24u*1024u*1024u)throw FormatError("personal snapshot exceeds bounded size");return b;
}
NativePersonalSnapshot decode_native_personal_snapshot(const std::vector<std::uint8_t>& b){
 if(b.size()>24u*1024u*1024u)throw FormatError("personal snapshot exceeds bounded size");std::size_t at=0;
 const auto get=[&](unsigned n){if(n>b.size()-at)throw FormatError("truncated personal snapshot");std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t{b[at++]}<<(8*i);return v;};
 const auto boolean=[&](){const auto v=get(1);if(v>1)throw FormatError("wardrobe boolean invalid");return v!=0;};
 const auto text=[&](){const auto n=get(2);if(n>4096||n>b.size()-at)throw FormatError("wardrobe text outside bound");std::string out(reinterpret_cast<const char*>(b.data()+at),static_cast<std::size_t>(n));at+=static_cast<std::size_t>(n);if(out.find('\0')!=std::string::npos)throw FormatError("wardrobe text contains NUL");return out;};
 const auto optional_text=[&]()->std::optional<std::string>{if(!boolean())return {};return text();};
 const auto asset=[&](){WardrobeCatalogAsset a;a.kind=static_cast<WardrobeAssetKind>(get(1));a.name=text();a.source=text();a.asset=text();for(auto& v:a.fingerprint)v=static_cast<std::uint8_t>(get(1));a.producer_ordinal=get(8);return a;};
 NativePersonalSnapshot s;s.version=static_cast<std::uint32_t>(get(4));for(auto& c:s.neighborhood_identity)c=static_cast<std::uint8_t>(get(1));
 const auto count=get(4);if(count>4096)throw FormatError("personal snapshot record count exceeds limit");
 for(std::uint64_t n=0;n<count;++n){NativePersonalRecord r;auto& id=r.identity;id.neighbor_id=signed16(static_cast<std::uint16_t>(get(2)));id.guid=static_cast<std::uint32_t>(get(4));id.definition_chunk_id=static_cast<std::uint16_t>(get(2));
  const auto length=get(2);if(length>4096||length>b.size()-at)throw FormatError("personal character asset exceeds limit");
  id.character_asset.assign(reinterpret_cast<const char*>(b.data()+at),static_cast<std::size_t>(length));at+=static_cast<std::size_t>(length);
  for(auto& c:id.character_fingerprint)c=static_cast<std::uint8_t>(get(1));r.revision=get(8);r.writer=static_cast<NativePersonalWriteSource>(get(1));
  r.projection.profile=static_cast<std::uint8_t>(get(1));r.projection.schema_version=static_cast<std::uint8_t>(get(1));
  const auto words=get(2);if(words!=0&&words!=80&&words!=256)throw FormatError("personal seed count invalid");
  if(words){r.seed_words.emplace();for(std::uint64_t i=0;i<words;++i)r.seed_words->push_back(signed16(static_cast<std::uint16_t>(get(2))));}
  for(std::size_t i=0;i<r.projection.present.size();++i){r.projection.present[i]=static_cast<std::uint8_t>(get(1));if(r.projection.present[i])r.projection.values[i]=signed16(static_cast<std::uint16_t>(get(2)));}
  s.records.push_back(std::move(r));
 }
 if(s.version>=2&&s.version<=4){const auto n=get(4);if(n>3u*1024u*1024u||n>b.size()-at)throw FormatError("personal catalog outside bounded snapshot");std::vector<std::uint8_t> raw(b.begin()+at,b.begin()+at+n);at+=n;if(n)s.catalog=decode_native_personal_catalog(raw);}
 if(s.version>=3&&s.version<=4){const auto n=get(4);if(n>4096)throw FormatError("wardrobe record count exceeds limit");
  for(std::uint64_t i=0;i<n;++i){NativeWardrobeRecord r;auto& id=r.identity;id.neighbor_id=signed16(static_cast<std::uint16_t>(get(2)));id.guid=static_cast<std::uint32_t>(get(4));id.definition_chunk_id=static_cast<std::uint16_t>(get(2));id.character_asset=text();for(auto& v:id.character_fingerprint)v=static_cast<std::uint8_t>(get(1));r.revision=get(8);
   auto& w=r.wardrobe;w.rule_profile=static_cast<std::uint32_t>(get(4));w.body_str_id=static_cast<std::uint16_t>(get(2));
   for(auto& v:w.source_fields)v=optional_text();for(auto& v:w.seed)v=optional_text();for(auto& v:w.strings)v=optional_text();
   for(auto& v:w.chosen)if(boolean()){v.emplace();v->literal=text();v->appearance=asset();v->texture=asset();}
   s.wardrobes.push_back(std::move(r));
  }
 }
 if(at!=b.size())throw FormatError("personal snapshot trailing bytes");validate_native_personal_snapshot(s);return s;
}
VmPrimitive native_personal_write_primitive(World& world,NativePersonalState* state,NativePersonalSourceResolver resolve){
 return [&world,state,resolve=std::move(resolve)](const VmFrame& frame,const Instruction& instruction,
        const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome {
  if(instruction.opcode!=1||instruction.operands[0]!=25||!state||!resolve||!dispatch.origin||dispatch.check_mode||
     !dispatch.ticket||!dispatch.origin->matches_active_primitive(frame,instruction,dispatch))
   return {PrimitiveDisposition::unsupported,"personal writer requires its actual ordinary Generic25 dispatch"};
  try{
   const auto selected=resolve(frame.context.caller);
   if(selected.body.id!=frame.context.caller||
      (frame.context.entity_references_bound&&selected.body.generation!=frame.context.caller_generation))
    throw FormatError("personal writer resolved another source caller");
   auto prepared=state->prepare(world,{selected},NativePersonalWriteSource::generic25);
   if(!state->commit(std::move(prepared),world))throw FormatError("personal writer source changed before publication");
   return {PrimitiveDisposition::goto_true,{}};
  }catch(const std::exception& error){return {PrimitiveDisposition::unsupported,error.what()};}
 };
}
} // namespace odh
