#include "odh/ts1_travel_return.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <sstream>
namespace odh {
Ts1TravelPersonInheritanceSource read_ts1_travel_person_inheritance(const View& source){
 constexpr std::uint64_t max_bytes=64u*1024u*1024u;
 if(source.size()>max_bytes)throw FormatError("travel personal inheritance IFF exceeds bounded source size");
 std::string owned(static_cast<std::size_t>(source.size()),'\0');
 source.read(0,owned.data(),owned.size());
 const auto identity=sha256(owned.data(),owned.size());
 // Parsing the owned copy prevents a mutable/seekable external stream from
 // supplying one set of bytes to the digest and another set to the parser.
 std::istringstream input(owned,std::ios::binary);View view(input,owned.size());
 std::optional<Nbrs> neighbors;
 iff_chunks(view,[&](const Chunk& chunk){if(chunk.type=="NBRS"&&chunk.id==1){
  if(neighbors)throw FormatError("travel personal inheritance has ambiguous NBRS1");
  neighbors=read_nbrs(chunk.payload);
 }});
 if(!neighbors)throw FormatError("travel personal inheritance requires NBRS1");
 return Ts1TravelPersonInheritanceSource(std::move(*neighbors),identity);
}
std::optional<std::int16_t> Ts1TravelPersonInheritanceSource::inherited_value(
 const Ts1TravelMember& member,std::size_t index) const {
 // Neighbor IDs are unique, while the pinned provider permits several IDs
 // with the same GUID. Inheritance uses this actual selected NID's record,
 // not the provider's separate last-GUID lookup for literal construction.
 const auto* selected=neighbors_.by_id(member.neighbor_id);
 if(!selected||selected->guid!=member.guid)
  throw FormatError("travel personal inheritance neighbor/GUID identity is missing or mismatched");
 const auto& record=*selected;
 if(record.version!=4&&record.version!=10)throw FormatError("travel personal inheritance record version is unsupported");
 if((record.person_mode>0)!=record.person_data.has_value()||
    (record.person_data&&record.person_data->size()!=(record.version==4?80u:256u)))
  throw FormatError("travel personal inheritance bank shape is inconsistent");
 // Pinned VMAvatar.InheritNeighbor956-969 copies88 runtime words. The NBRS
 // reader zero-extends a PRESENT old80-word bank; absent PersonData keeps the
 // constructor bank and must never be treated as a zero-filled inheritance.
 // These five fields are preserved/rebound/reset rather than copied raw.
 if(!record.person_data||index>=88||index==31||index==32||index==34||index==35||index==65)return {};
 return index<record.person_data->size()?(*record.person_data)[index]:std::int16_t{0};
}
namespace {
const SessionHouseholdMember& member(const SessionBundle& bundle,const Ts1TravelMember& identity){
 const auto found=std::find_if(bundle.household_roster.begin(),bundle.household_roster.end(),[&](const auto& m){return m.guid==identity.guid&&m.neighbor_id==identity.neighbor_id&&m.object_definition_id==identity.definition_chunk_id&&m.character_asset==identity.character_asset&&m.character_fingerprint==identity.character_fingerprint;});
 if(found==bundle.household_roster.end())throw FormatError("travel return member resource identity changed");return *found;
}
const Entity& avatar(const SessionBundle& bundle,const SessionHouseholdMember& identity){
 const auto found=std::find_if(bundle.world.entities.begin(),bundle.world.entities.end(),[&](const auto& e){return e.id==identity.id&&e.generation==identity.generation;});
 if(found==bundle.world.entities.end()||found->dead||found->kind!=EntityKind::avatar||!found->definition||found->definition->guid!=identity.guid||found->person_data.size()<=61||found->person_data[31]!=identity.neighbor_id||static_cast<std::uint16_t>(found->person_data[61])!=bundle.host.family_chunk_id)throw FormatError("travel return avatar no longer matches live family resource identity");return *found;
}
bool proven_lot_local_word(std::size_t index){
 // F pin VMAvatar.cs612 tick counter;659-708 local object-ID head seek;
 // VMThread.cs263/269/867/875 queue priority/noninterruptible state;
 // VMRoutingFrame.cs207/1090 local route result/entry flags. SavedLot restores
 // the HOME counterparts together with their owning threads/controllers.
 // TS1 words75..83 are NOT the TSO target/decor aliases: keep them unresolved.
 // VMAvatar.InheritNeighbor preserves the fresh constructor PersonType32 and
 // clears GreetStatus34; VMThread899 and VMFindBestAction105/123 use them to
 // classify local visitors. SavedLot restores HOME's role/greeting state.
 switch(index){case 27:case 32:case 33:case 34:case 37:case 41:case 42:case 43:case 44:case 45:case 62:case 71:return true;default:return false;}
}
bool proven_action_workspace_flag(const Ts1TravelPersonalDelta& delta){
 // PersonData25 is TreeUseDataField in the pinned reference. The actual
 // ToiletStall_SP4098 action sets bit0x40 before walking/using the toilet and
 // clears it on every ordinary success/failure exit (pc1 vs10/11/29/30).
 // A saved actor can still be inside that action when Generic17 switches lots.
 // Only this flag difference is classified; other bits or absent words remain
 // unresolved. No object/GUID-specific exception and no visited flag import.
 return delta.index==25&&delta.home_value&&delta.visited_value&&
  (static_cast<std::uint16_t>(*delta.home_value)^static_cast<std::uint16_t>(*delta.visited_value))==0x40u;
}
void recount(RelationshipSnapshot& state){
 state.pair_count=state.value_count=0;
 for(const auto& source:state.sources){if(source.pairs.size()>state.limits.pairs-state.pair_count)throw FormatError("travel return relationship pair budget exceeded");state.pair_count+=source.pairs.size();for(const auto& pair:source.pairs){if(pair.values.size()>state.limits.values-state.value_count)throw FormatError("travel return relationship value budget exceeded");state.value_count+=pair.values.size();}}
 RelationshipState checked(state.limits);if(!checked.restore_snapshot(state))throw FormatError("travel return relationship projection is invalid or exceeds home bounds");
}
}
// Binding reference:
// S e36e83f95d44b9f4b378e7665c4e2ecbaa88b4ab TS1GameScreen.cs425-440,
// 612-620 reloads SavedLot then ActivateFamily: home entities/threads/clock stay.
// F 7e76c1960252ddd033d8937d5edb47d326f5b69c VM.cs699-745 and
// VMAvatar.cs1205 clone entity/PersonData state; no later personal merge here.
// VMGenericTS1Call.cs97-146/185-207 carries needs through neighbor inventory;
// Generic21 must consume them in the actual home authored continuation.
// VMRelationship.cs63-91 uses neighborhood Neighbor.Relationships (NID authority).
// VMTS1Budget.cs18-21/VMTS1GlobalLinkStub.cs70-99 share CurrentFamily.Budget.
// Generic25 at227-229 is a successful no-op, NOT a persistent-person schema.
PreparedTs1TravelReturn prepare_ts1_travel_return(const SessionBundle& envelope,SessionBundleLimits limits,
 const Ts1TravelPersonInheritanceSource* inheritance){
 const auto encoded=encode_session_bundle(envelope,limits);
 if(!envelope.travel||!envelope.travel_home_session||!envelope.travel_visited_session)throw FormatError("travel return requires a durable home/visited envelope");
 const auto& state=*envelope.travel;
 if(inheritance&&inheritance->neighborhood_identity()!=state.neighborhood_identity)
  throw FormatError("travel personal inheritance source differs from the authenticated neighborhood identity");
 if(!state.pending||state.current_house==state.home_house||(state.pending->destination!=-1&&state.pending->destination!=static_cast<std::int16_t>(state.home_house)))throw FormatError("travel return requires an authenticated deferred request to the permanent home");
 const auto active=decode_session_bundle(*envelope.travel_visited_session,std::nullopt,limits);
 PreparedTs1TravelReturn result;result.home_=decode_session_bundle(*envelope.travel_home_session,std::nullopt,limits);
 result.source_checkpoint_sha256_=sha256(encoded.data(),encoded.size());result.suspended_home_sha256_=sha256(envelope.travel_home_session->data(),envelope.travel_home_session->size());
 NeighborInventory inventory(Ngbh{});inventory.restore(*active.neighbor_inventory);
 constexpr unsigned need_motives[7]={9,6,5,15,7,8,14}; // Type2 GUID0..6 source order.
 for(const auto& identity:ts1_travel_departure_members(state)){
  const auto& home_actor=avatar(result.home_,member(result.home_,identity));const auto& visited_actor=resolve_ts1_travel_departure_avatar(active,identity);
  if(visited_actor.motives.size()!=16)throw FormatError("travel return requires the actual source motive bank");
  for(unsigned key=0;key<7;++key){const auto token=inventory.get_data(identity.neighbor_id,key);if(!token||*token!=static_cast<std::uint16_t>(visited_actor.motives[need_motives[key]]))throw FormatError("travel return motive token differs from the sealed source avatar");}
  const auto size=std::max(home_actor.person_data.size(),visited_actor.person_data.size());
  // Resolve/bind every selected identity even if it happens to have no delta.
  // A malformed source cannot become trusted because all values match HOME.
  if(inheritance)(void)inheritance->inherited_value(identity,0);
  for(std::size_t index=0;index<size;++index){
   const auto old=index<home_actor.person_data.size()?std::optional<std::int16_t>{home_actor.person_data[index]}:std::nullopt;
   const auto current=index<visited_actor.person_data.size()?std::optional<std::int16_t>{visited_actor.person_data[index]}:std::nullopt;
   if(old!=current){
    Ts1TravelPersonalDelta delta{identity,index,old,current};
    if(old&&current&&(proven_lot_local_word(index)||proven_action_workspace_flag(delta)))result.runtime_deltas_.push_back(std::move(delta));
    else if(old&&current&&inheritance&&inheritance->inherited_value(identity,index)==current)
     result.inherited_deltas_.push_back(std::move(delta));
    else result.personal_deltas_.push_back(std::move(delta));
   }
  }
 }
 // Neighbor rows are one neighborhood-wide mutable authority, including NPC
 // and nontraveling-person effects. OBJECT rows remain exact home identities.
 auto& relationships=result.home_.relationships;
 relationships.sources.erase(std::remove_if(relationships.sources.begin(),relationships.sources.end(),[](const auto& source){return source.matrix==RelationshipMatrix::neighbor;}),relationships.sources.end());
 for(const auto& source:active.relationships.sources)if(source.matrix==RelationshipMatrix::neighbor)relationships.sources.push_back(source);
 std::sort(relationships.sources.begin(),relationships.sources.end(),[](const auto& a,const auto& b){if(a.matrix!=b.matrix)return a.matrix<b.matrix;if(a.id!=b.id)return a.id<b.id;return a.generation<b.generation;});recount(relationships);
 result.home_.host.family_budget=active.host.family_budget;result.home_.neighbor_inventory=active.neighbor_inventory;
 if(result.home_.type_attributes&&!active.type_attributes)throw FormatError("return dropped shared neighborhood type attributes");
 result.home_.type_attributes=active.type_attributes;
 if(result.home_.family_spells&&!active.family_spells)throw FormatError("return session dropped its originating family spell carrier");
 if(active.family_spells){
  auto merged=*active.family_spells;
  if(result.home_.family_spells){
   const auto& home=*result.home_.family_spells;
   if(home.family_chunk_id!=merged.family_chunk_id||home.family_number!=merged.family_number||
      home.loaded_presence!=merged.loaded_presence||home.extension_dword!=merged.extension_dword)
    throw FormatError("return family spell carrier identity/source metadata mismatch");
   for(std::size_t i=0;i<merged.words.size();++i)merged.words[i]=std::max(merged.words[i],home.words[i]);
  }
  result.home_.family_spells=merged;
 }
 // Leave effects0..6, time7/8 and companion tokens intact. Only the real
 // authored primitives consume them; candidate preparation has no such effect.
 result.home_.travel.reset();result.home_.travel_home_session.reset();result.home_.travel_visited_session.reset();
 (void)encode_session_bundle(result.home_,limits);return result;
}
}
