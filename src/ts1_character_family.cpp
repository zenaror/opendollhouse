// SPDX-License-Identifier: MPL-2.0
#include "odh/ts1_character_family.hpp"
#include "odh/neighbor_context.hpp"
#include "odh/family_overlay.hpp"
#include <algorithm>
#include <limits>
#include <set>
#include <sstream>
namespace odh {namespace {
constexpr std::size_t maximum_bytes=family_overlay_max_bytes;
constexpr NbrsLimits neighbor_limits{maximum_bytes,maximum_bytes,4096,256,64*1024,16384,65536};
bool nonzero(const std::array<std::uint8_t,32>& v){return std::any_of(v.begin(),v.end(),[](auto b){return b!=0;});}
struct Writer {
 std::vector<std::uint8_t> bytes;
 void number(std::uint64_t v,unsigned n){if(n>maximum_bytes-bytes.size())throw FormatError("character/family snapshot byte budget exceeded");for(unsigned i=0;i<n;++i)bytes.push_back(static_cast<std::uint8_t>(v>>(8*i)));}
 void raw(const std::uint8_t* b,std::size_t n){if(n>maximum_bytes-bytes.size())throw FormatError("character/family snapshot byte budget exceeded");bytes.insert(bytes.end(),b,b+n);}
 void string(const std::string& s){number(s.size(),4);raw(reinterpret_cast<const std::uint8_t*>(s.data()),s.size());}
};
struct Reader {
 const std::vector<std::uint8_t>& bytes;std::size_t at=0;
 std::uint64_t number(unsigned n){if(n>bytes.size()-at)throw FormatError("truncated character/family snapshot");std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t{bytes[at++]}<<(8*i);return v;}
 std::string string(){const auto n=number(4);if(n>4096||n>bytes.size()-at)throw FormatError("character identity string exceeds bounds");std::string s(reinterpret_cast<const char*>(bytes.data()+at),static_cast<std::size_t>(n));at+=static_cast<std::size_t>(n);return s;}
};
bool same_seed(const NeighborRecord& a,const NeighborRecord& b){
 Nbrs left{0,73,{{1,a}},0},right{0,73,{{1,b}},0};return encode_nbrs(left,neighbor_limits)==encode_nbrs(right,neighbor_limits);
}
}
void validate_ts1_character_family_snapshot(const Ts1CharacterFamilySnapshot& s){
 if(s.version!=1||!nonzero(s.neighborhood_identity)||s.characters.size()>1024||s.rosters.size()>4096||
    ((!s.characters.empty()||!s.rosters.empty())&&!s.revision))throw FormatError("invalid character/family snapshot profile or budget");
 std::set<std::int16_t> ids;std::set<std::uint32_t> guids;std::size_t text=0;
 for(const auto& c:s.characters){validate_ts1_created_character(c);const auto& id=c.identity;
  if(!ids.insert(id.neighbor_id).second||!guids.insert(id.guid).second)throw FormatError("created character identities repeat");
  text+=id.character_asset.size();if(text>2u*1024u*1024u)throw FormatError("created character provenance budget exceeded");
 }
 std::optional<std::uint16_t> prior;
 for(const auto& f:s.rosters){
  if((prior&&f.family_chunk_id<=*prior)||!nonzero(f.source_identity)||!f.revision||f.revision>s.revision||f.members.size()>256)
    throw FormatError("invalid current family roster/order/revision");
  prior=f.family_chunk_id; // Source FAMI is an ordered list, not a GUID set.
 }
}
void validate_ts1_character_family_successor(const Ts1CharacterFamilySnapshot& a,const Ts1CharacterFamilySnapshot& b){
 validate_ts1_character_family_snapshot(a);validate_ts1_character_family_snapshot(b);
 if(a.neighborhood_identity!=b.neighborhood_identity||b.revision<a.revision||b.characters.size()<a.characters.size())throw FormatError("character/family state cannot change source or lose history");
 for(std::size_t i=0;i<a.characters.size();++i)if(!same_native_personal_identity(a.characters[i].identity,b.characters[i].identity)||!same_seed(a.characters[i].seed,b.characters[i].seed))
  throw FormatError("created character history changed");
 for(const auto& old:a.rosters){const auto* row=find_ts1_family_roster(b,old.family_chunk_id);
  if(!row||row->source_identity!=old.source_identity||row->revision<old.revision||
    (row->revision==old.revision&&(row->members!=old.members)))throw FormatError("family roster successor changes identity or rolls back a revision");
 }
 if(b.revision==a.revision&&encode_ts1_character_family_snapshot(a)!=encode_ts1_character_family_snapshot(b))throw FormatError("same character/family revision has different state");
}
std::vector<std::uint8_t> encode_ts1_character_family_snapshot(const Ts1CharacterFamilySnapshot& s){
 validate_ts1_character_family_snapshot(s);Writer w;w.number(s.version,4);w.raw(s.neighborhood_identity.data(),32);w.number(s.revision,8);w.number(s.characters.size(),4);
 for(const auto& c:s.characters){const auto& id=c.identity;w.number(static_cast<std::uint16_t>(id.neighbor_id),2);w.number(id.guid,4);w.number(id.definition_chunk_id,2);w.string(id.character_asset);w.raw(id.character_fingerprint.data(),32);
  const auto n=encode_nbrs(Nbrs{0,73,{{1,c.seed}},0},neighbor_limits);w.number(n.size(),4);w.raw(n.data(),n.size());}
 w.number(s.rosters.size(),4);for(const auto& f:s.rosters){w.number(f.family_chunk_id,2);w.raw(f.source_identity.data(),32);w.number(f.revision,8);w.number(f.members.size(),2);for(auto guid:f.members)w.number(guid,4);}
 return std::move(w.bytes);
}
Ts1CharacterFamilySnapshot decode_ts1_character_family_snapshot(const std::vector<std::uint8_t>& bytes){
 if(bytes.size()>maximum_bytes)throw FormatError("character/family wire exceeds byte budget");Reader r{bytes};Ts1CharacterFamilySnapshot s;s.version=static_cast<std::uint32_t>(r.number(4));for(auto& b:s.neighborhood_identity)b=static_cast<std::uint8_t>(r.number(1));s.revision=r.number(8);
 const auto count=r.number(4);if(count>1024)throw FormatError("created character count exceeds budget");
 for(std::uint64_t i=0;i<count;++i){Ts1CreatedCharacter c;c.identity.neighbor_id=static_cast<std::int16_t>(r.number(2));c.identity.guid=static_cast<std::uint32_t>(r.number(4));c.identity.definition_chunk_id=static_cast<std::uint16_t>(r.number(2));c.identity.character_asset=r.string();for(auto& b:c.identity.character_fingerprint)b=static_cast<std::uint8_t>(r.number(1));
  const auto n=r.number(4);if(n>maximum_bytes||n>bytes.size()-r.at)throw FormatError("created neighbor wire exceeds bounds");std::string raw(reinterpret_cast<const char*>(bytes.data()+r.at),static_cast<std::size_t>(n));r.at+=static_cast<std::size_t>(n);std::istringstream stream(raw,std::ios::in|std::ios::binary);const auto neighbors=read_nbrs(View(stream,raw.size()),neighbor_limits);
  if(neighbors.reserved||neighbors.version!=73||neighbors.remaining_bytes||neighbors.entries.size()!=1||!neighbors.entries[0].record)throw FormatError("created neighbor wire profile differs");c.seed=*neighbors.entries[0].record;s.characters.push_back(std::move(c));}
 const auto families=r.number(4);if(families>4096)throw FormatError("changed family count exceeds budget");for(std::uint64_t i=0;i<families;++i){Ts1FamilyRoster f;f.family_chunk_id=static_cast<std::uint16_t>(r.number(2));for(auto& b:f.source_identity)b=static_cast<std::uint8_t>(r.number(1));f.revision=r.number(8);const auto count_members=r.number(2);if(count_members>256)throw FormatError("current family member count exceeds limit");for(std::uint64_t n=0;n<count_members;++n)f.members.push_back(static_cast<std::uint32_t>(r.number(4)));s.rosters.push_back(std::move(f));}
 if(r.at!=bytes.size())throw FormatError("trailing character/family snapshot bytes");validate_ts1_character_family_snapshot(s);return s;
}
const Ts1FamilyRoster* find_ts1_family_roster(const Ts1CharacterFamilySnapshot& s,std::uint16_t id){const auto i=std::lower_bound(s.rosters.begin(),s.rosters.end(),id,[](const auto& f,auto key){return f.family_chunk_id<key;});return i!=s.rosters.end()&&i->family_chunk_id==id?&*i:nullptr;}
Nbrs apply_ts1_character_neighbors(const Nbrs& source,const Ts1CharacterFamilySnapshot& s){
 validate_ts1_character_family_snapshot(s);
 // The same owner serves runtime hydration, overlay replay and cold load.
 // NeighborID names its manager slot; creation fills a real hole instead of
 // appending a second serialized slot that changes the next allocation.
 std::vector<NeighborRecord> owned;owned.reserve(s.characters.size());
 for(const auto& c:s.characters)owned.push_back(c.seed);
 return merge_owned_neighbor_seeds(source,owned);
}
std::vector<FamilyEntry> apply_ts1_family_rosters(const std::vector<FamilyEntry>& source,const Ts1CharacterFamilySnapshot& s){
 validate_ts1_character_family_snapshot(s);auto result=source;
 for(const auto& row:s.rosters){const auto i=std::find_if(result.begin(),result.end(),[&](const auto& f){return f.chunk_id==row.family_chunk_id;});
  if(i==result.end()||family_overlay_identity(*i)!=row.source_identity)throw FormatError("family roster differs from immutable family identity");i->family.member_guids=row.members;}
 return result;
}
NeighborRecord make_simitone_character_neighbor(std::int16_t nid,std::uint32_t guid,const std::vector<std::int16_t>& base){
 if(nid<=0||!guid||base.size()!=88)throw FormatError("character creation needs exact GUID/NID and88template words");NeighborRecord n{};n.version=10;n.unknown3=9;n.name="iffname";n.person_mode=9;n.person_data=std::vector<std::int16_t>(256);std::copy(base.begin(),base.end(),n.person_data->begin());n.neighbor_id=nid;n.guid=guid;n.unknown_neg_one=-1;return n;
}
NeighborRecord make_original_character_neighbor(std::int16_t nid,std::uint32_t guid,const std::string& name){
 if(nid<=0||!guid||name.empty()||name.size()>256||name.find('\0')!=std::string::npos)throw FormatError("original character needs exact GUID/NID/resource name");
 NeighborRecord n{};n.version=10;n.unknown3=0;n.name=name;if(name.size()%2==0)n.name_padding=0;
 n.neighbor_id=nid;n.guid=guid;n.person_mode=0;n.unknown_neg_one=-1;return n;
}
std::int16_t allocate_ts1_neighbor_id(const Nbrs& source,const std::vector<std::int16_t>& reserved){
 std::set<std::int16_t> used;for(auto nid:reserved)if(nid>0)used.insert(nid);for(const auto& e:source.entries)if(e.record&&e.record->neighbor_id>0)used.insert(e.record->neighbor_id);
 for(std::int32_t nid=1;nid<=INT16_MAX;++nid)if(!used.count(static_cast<std::int16_t>(nid)))return static_cast<std::int16_t>(nid);throw FormatError("no free TS1 neighbor ID");
}
std::int16_t allocate_original_neighbor_id(const std::vector<std::int16_t>& present,std::size_t extent,const std::set<std::int16_t>& reserved){
 if(extent>4096||present.size()>4096)throw FormatError("original neighbor manager exceeds bounded4096");
 std::vector<bool> used(extent,false);for(auto nid:present){if(nid<=0||static_cast<std::size_t>(nid)>extent||used[static_cast<std::size_t>(nid-1)])throw FormatError("original neighbor manager has inconsistent slot identities");used[static_cast<std::size_t>(nid-1)]=true;}
 std::size_t selected=extent;for(std::size_t i=0;i<used.size();++i)if(!used[i])selected=i;
 if(selected>=4096)throw FormatError("original neighbor manager exhausted bounded4096 slots");
 const auto nid=static_cast<std::int16_t>(selected+1);if(reserved.count(nid))throw FormatError("source-selected neighbor slot remains protected by native history");return nid;
}
void select_ts1_runtime_whole_family(std::vector<std::uint32_t>& subset,const std::vector<std::uint32_t>& members){
 if(members.size()>256)throw FormatError("whole-family runtime subset exceeds native bound");
 std::set<std::uint32_t> seen;for(auto guid:members)if(!guid||!seen.insert(guid).second)throw FormatError("whole-family runtime subset has invalid or duplicate identity");
 auto selected=members;subset.swap(selected);
}
void admit_ts1_runtime_subset_member(std::vector<std::uint32_t>& subset,std::uint32_t guid){
 if(!guid)throw FormatError("runtime family subset cannot admit zero GUID");
 if(std::find(subset.begin(),subset.end(),guid)!=subset.end())return;
 if(subset.size()>=256)throw FormatError("runtime family subset exceeds bounded256");subset.push_back(guid);
}
void refresh_ts1_family_person_types(World& world,const std::vector<std::uint32_t>& family,bool class_one){
 if(family.size()>256)throw FormatError("family PersonType refresh roster exceeds bounded256");
 const auto people=world.snapshot();
 for(const auto& person:people.entities){
  if(person.kind!=EntityKind::avatar||person.dead||person.ui_ghost)continue;
  if(!person.definition||person.person_data.size()<=32)throw FormatError("family PersonType refresh requires exact avatar definition/bank");
  auto value=person.person_data[32];if(value==0)value=1;
  if(std::find(family.begin(),family.end(),person.definition->guid)!=family.end())value=0;
  if(!class_one&&value==0)value=1;
  const auto* now=world.find(person.id);
  if(!now||now->generation!=person.generation||now->dead||!world.set_person_data(person.id,32,value))
    throw FormatError("family PersonType refresh lost exact live generation");
 }
}

}

namespace odh {namespace {
std::int16_t family_word(std::uint16_t id){return static_cast<std::int16_t>(id<32768?id:static_cast<std::int32_t>(id)-65536);}
void consumer(bool accepted,const std::string& why,const char* service){if(!accepted)throw FormatError(std::string(service)+": "+why);}
}
Ts1CharacterFamilyState::Ts1CharacterFamilyState(Ts1CharacterFamilySnapshot s,const std::vector<FamilyEntry>& effective,
    std::map<std::uint16_t,std::array<std::uint8_t,32>> identities):state_(std::move(s)),identities_(std::move(identities)){
 validate_ts1_character_family_snapshot(state_);
 for(const auto& f:effective)if(!families_.emplace(f.chunk_id,f).second)throw FormatError("character/family owner has ambiguous family identity");
 for(const auto& row:state_.rosters){const auto f=families_.find(row.family_chunk_id);const auto id=identities_.find(row.family_chunk_id);
  if(f==families_.end()||id==identities_.end()||id->second!=row.source_identity||f->second.family.member_guids!=row.members)
   throw FormatError("character/family owner differs from admitted overlay replay");}
 for(const auto& f:families_)if(!identities_.count(f.first))throw FormatError("character/family owner lacks stable family lease identity");
}
const FamilyEntry* Ts1CharacterFamilyState::retained_family(std::uint16_t id) const {const auto f=families_.find(id);return f==families_.end()?nullptr:&f->second;}
const FamilyEntry* Ts1CharacterFamilyState::family(std::uint16_t id) const {return retained_family(id);}
void Ts1CharacterFamilyState::add_created(const Ts1CreatedCharacter& c){
 validate_ts1_created_character(c);if(state_.characters.size()>=1024||state_.revision==UINT64_MAX)throw FormatError("character creation count/revision exhausted");
 for(const auto& old:state_.characters)if(old.identity.guid==c.identity.guid||old.identity.neighbor_id==c.identity.neighbor_id)throw FormatError("created character identity reused");
 auto next=state_;++next.revision;next.characters.push_back(c);validate_ts1_character_family_snapshot(next);state_=std::move(next);
}
void Ts1CharacterFamilyState::changed(FamilyEntry& f,const Ts1FamilyConsumers& c){
 if(state_.revision==UINT64_MAX)throw FormatError("family roster revision exhausted");
 auto row=std::lower_bound(state_.rosters.begin(),state_.rosters.end(),f.chunk_id,[](const auto& r,auto id){return r.family_chunk_id<id;});
 Ts1FamilyRoster next{f.chunk_id,identities_.at(f.chunk_id),state_.revision+1,f.family.member_guids};
 if(row==state_.rosters.end()||row->family_chunk_id!=f.chunk_id)state_.rosters.insert(row,std::move(next));else *row=std::move(next);
 ++state_.revision;
 if(c.family_assets){const auto assets=c.family_assets(f.chunk_id);if(!assets)throw FormatError("family asset owner unavailable during FAMI membership publication");f.family.budget=assets->first;f.family.value_in_arch=assets->second;}
 std::string why;consumer(c.publish_family&&c.publish_family(f,true,why),why,"family provider publication");
}
bool Ts1CharacterFamilyState::add_to_family(std::optional<std::uint16_t> target,const NeighborRecord& n,
    const Ts1FamilyConsumers& c,bool append_subset){
 if(!target)return false;auto destination=families_.find(*target);if(destination==families_.end())return false;
 if(n.neighbor_id<=0||!n.guid)throw FormatError("family addition neighbor identity absent");
 const auto nid=n.neighbor_id;const auto guid=n.guid;std::string why;
 if(!c.neighbor_family)throw FormatError("family addition neighbor field service absent");
 const auto old=c.neighbor_family(nid);if(!old)throw FormatError("family addition neighbor person61 unknown");
 // Resolve destination before removing the old family. This also preserves the
 // exact destination object during a same-family remove/reappend operation.
 if(*old!=0&&!remove_from_family(n,c))return false;
 auto& members=destination->second.family.member_guids;
 if(std::find(members.begin(),members.end(),guid)!=members.end())return false;
 if(members.size()>=256)throw FormatError("family GUID vector exceeds bounded native256 storage");
 members.push_back(guid);changed(destination->second,c);
 consumer(c.refresh_person_types&&c.refresh_person_types(why),why,"family PersonType refresh");
 if(std::find(members.begin(),members.end(),guid)==members.end())return false;
 consumer(c.set_runtime_family&&c.set_runtime_family(nid,family_word(*target),true,why),why,"runtime avatar family61/lingering67");
 consumer(c.set_neighbor_family&&c.set_neighbor_family(nid,family_word(*target),true,why),why,"neighbor family61/lingering67");
 consumer(c.family_changed&&c.family_changed(*target,why),why,"family changed event");
 if(append_subset)consumer(c.append_runtime_subset&&c.append_runtime_subset(*target,guid,why),why,"current family runtime subset");
 return true;
}
bool Ts1CharacterFamilyState::remove_from_family(const NeighborRecord& n,const Ts1FamilyConsumers& c){
 if(n.neighbor_id<=0||!n.guid)throw FormatError("family removal neighbor identity absent");
 const auto nid=n.neighbor_id;const auto guid=n.guid;std::string why;
 if(!c.neighbor_family)throw FormatError("family removal neighbor field service absent");
 const auto old=c.neighbor_family(nid);if(!old)throw FormatError("family removal person61 unknown");
 if(*old==0)return false;const auto id=static_cast<std::uint16_t>(*old);auto f=families_.find(id);if(f==families_.end())return false;
 auto& members=f->second.family.member_guids;const auto i=std::find(members.begin(),members.end(),guid);if(i==members.end())return false;
 consumer(c.set_runtime_family&&c.set_runtime_family(nid,0,false,why),why,"runtime avatar family61");
 consumer(c.set_neighbor_family&&c.set_neighbor_family(nid,0,false,why),why,"neighbor family61");
 members.erase(i);changed(f->second,c);
 consumer(c.refresh_person_types&&c.refresh_person_types(why),why,"family PersonType refresh");
 consumer(c.family_changed&&c.family_changed(id,why),why,"family changed event");return true;
}
bool Ts1CharacterFamilyState::combine_assets(std::optional<std::uint16_t> current,std::uint16_t source,const Ts1FamilyConsumers& c){
 if(!current)throw FormatError("family asset combination lacks CurrentFamily");auto f=families_.find(*current);const auto* donor=family(source);
 if(f==families_.end()||!donor)throw FormatError("family asset combination source family absent");
 if(c.family_assets){
  const auto target_assets=c.family_assets(*current),source_assets=c.family_assets(source);
  if(!target_assets||!source_assets)throw FormatError("family asset combination owner unavailable");
  f->second.family.budget=target_assets->first;f->second.family.value_in_arch=target_assets->second;
  auto& source_family=families_.at(source).family;source_family.budget=source_assets->first;source_family.value_in_arch=source_assets->second;
 }
 const auto sum=static_cast<std::uint32_t>(f->second.family.budget)+static_cast<std::uint32_t>(donor->family.value_in_arch)+static_cast<std::uint32_t>(donor->family.budget);
 f->second.family.budget=static_cast<std::int32_t>(sum<=INT32_MAX?sum:static_cast<std::int64_t>(sum)-4294967296LL);
 // Budget remains carried by existing HOST/checkpoint assignment, not copied
 // into the membership receipt as a second source of money.
 std::string why;consumer(c.publish_family&&c.publish_family(f->second,family(*current)!=nullptr,why),why,"family combined assets");return true;
}
bool Ts1CharacterFamilyState::verify_current(std::optional<std::uint16_t> current,const Ts1FamilyConsumers& c){
 // Source SelectWholeFamily on CurrentFamily precedes VerifyFamily. An absent
 // CurrentFamily is a source error here, not a successful no-op.
 if(!current||!retained_family(*current))throw FormatError("purchased-pet verification lacks CurrentFamily");std::string why;consumer(c.verify_family&&c.verify_family(why),why,"SelectWholeFamily/VerifyFamily");return true;
}
VmPrimitive ts1_family_primitive(Ts1FamilyPrimitiveServices services){
 return [services=std::move(services)](const VmFrame& frame,const Instruction& i,const Ts1Temps& temps,const VmDispatch& dispatch)->PrimitiveOutcome{
  if(i.opcode!=1||(i.operands[0]!=4&&i.operands[0]!=5&&i.operands[0]!=6&&i.operands[0]!=32&&i.operands[0]!=33))return {};
  const auto fail=[](const std::string& why){return PrimitiveOutcome{PrimitiveDisposition::unsupported,"TS1 character/family: "+why};};
  const auto lease=dispatch.origin?dispatch.origin->instruction_lease(frame):std::nullopt;
  if(!lease||!lease->matches(frame,i,dispatch)||!services.state||!services.current_family)return fail("actual source instruction/state binding absent");
  if(i.operands[1])return fail("complete GenericCall WORD mode is unsupported");
  try{const auto current=services.current_family();bool result=false;
   if(i.operands[0]==5)result=services.state->combine_assets(current,static_cast<std::uint16_t>(temps[0]),services.consumers);
   else if(i.operands[0]==32)result=services.state->verify_current(current,services.consumers);
   else{
    if(!services.neighbors)return fail("neighbor provider absent");const auto* n=services.neighbors->by_id(frame.context.stack_object);
    if(!n)return fail("source neighbor identity is missing");
    if(i.operands[0]==6)result=services.state->remove_from_family(*n,services.consumers);
    else {
      const auto target=i.operands[0]==33?std::optional<std::uint16_t>{static_cast<std::uint16_t>(temps[0])}:current;
      if(target&&!services.state->family(*target))return {PrimitiveDisposition::goto_false,{}};
      result=services.state->add_to_family(target,*n,services.consumers,true);
    }
   }
   return {result?PrimitiveDisposition::goto_true:PrimitiveDisposition::goto_false,{}};
  }catch(const std::exception& e){return fail(e.what());}
 };
}
}
