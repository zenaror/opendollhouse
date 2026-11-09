// SPDX-License-Identifier: MPL-2.0
#include "odh/original_frontend_personal.hpp"
#include "odh/sha256.hpp"
#include <sstream>
#include <algorithm>
namespace odh {
std::optional<bool> original_frontend_personal_save_required(const OriginalFrontendSaveBoundary& input) noexcept {
    if(input.simless!=original_frontend_playing_simless)return false;
    const auto house=input.house;
    if((house>=21&&house<=30)||(house>=81&&house<=89)||(house>=93&&house<=98))return true;
    const bool vacation=house>=40&&house<=49;
    if(input.zoning&&*input.zoning==OriginalLotClass::community)return true;
    if(vacation&&input.family_number&&*input.family_number==0)return true;
    if(!input.zoning||(vacation&&!input.family_number))return std::nullopt;
    return false;
}

struct OriginalFrontendPersonalSource::Data {
    std::string owned;
    Nbrs neighbors, raw;
    std::vector<NeighborRecord> owned_neighbors;
    void rebuild(){neighbors=catalog?hydrate_owned_native_personal_neighbors(raw,*catalog,owned_neighbors):merge_owned_neighbor_seeds(raw,owned_neighbors);}
    std::optional<NativePersonalCatalog> catalog;
    std::optional<Ts1CharacterFamilySnapshot> created;
    std::array<std::uint8_t,32> identity{};
};
OriginalFrontendPersonalSource OriginalFrontendPersonalSource::with_catalog(const NativePersonalCatalog& catalog) const {
    if(catalog.neighborhood_identity!=data_->identity)throw FormatError("frontend hydrated catalog neighborhood differs");
    if(data_->catalog){if(encode_native_personal_catalog(*data_->catalog)!=encode_native_personal_catalog(catalog))throw FormatError("frontend hydrated catalog changed");return *this;}
    auto data=std::make_shared<Data>(*data_);data->catalog=catalog;data->rebuild();return OriginalFrontendPersonalSource(std::move(data));
}
OriginalFrontendPersonalSource OriginalFrontendPersonalSource::with_owned_neighbors(const std::vector<NeighborRecord>& owned) const {
    auto data=std::make_shared<Data>(*data_);
    // Full immutable receipts: an existing seed cannot silently disappear or
    // change merely because its avatar left FAMI or the current house.
    const auto supplied=merge_owned_neighbor_seeds(data->raw,owned);
    (void)merge_owned_neighbor_seeds(supplied,data->owned_neighbors);
    for(const auto& old:data->owned_neighbors)
        if(std::none_of(owned.begin(),owned.end(),[&](const auto& n){return n.neighbor_id==old.neighbor_id&&n.guid==old.guid;}))
            throw FormatError("frontend owned neighbor receipt disappeared");
    data->owned_neighbors=owned;data->rebuild();return OriginalFrontendPersonalSource(std::move(data));
}
OriginalFrontendPersonalSource OriginalFrontendPersonalSource::with_created_characters(const Ts1CharacterFamilySnapshot& snapshot) const {
    if(snapshot.neighborhood_identity!=data_->identity)throw FormatError("frontend created-character neighborhood differs");
    validate_ts1_character_family_snapshot(snapshot);
    if(data_->created)validate_ts1_character_family_successor(*data_->created,snapshot);
    auto owned=data_->owned_neighbors;
    for(const auto& c:snapshot.characters)if(std::none_of(owned.begin(),owned.end(),[&](const auto& n){return n.neighbor_id==c.seed.neighbor_id;}))owned.push_back(c.seed);
    auto result=with_owned_neighbors(owned);
    auto data=std::make_shared<Data>(*result.data_);data->created=snapshot;
    // Validate existing matching receipts as well as newly appended seeds.
    std::vector<NeighborRecord> created;for(const auto& c:snapshot.characters)created.push_back(c.seed);
    (void)merge_owned_neighbor_seeds(data->neighbors,created);
    return OriginalFrontendPersonalSource(std::move(data));
}
bool OriginalFrontendPersonalSource::is_created_neighbor(std::int16_t nid,std::uint32_t guid) const {
    return std::any_of(data_->owned_neighbors.begin(),data_->owned_neighbors.end(),[&](const auto& n){return n.neighbor_id==nid&&n.guid==guid;});
}
const NativePersonalCatalog* OriginalFrontendPersonalSource::catalog() const {return data_->catalog?&*data_->catalog:nullptr;}
const Nbrs& OriginalFrontendPersonalSource::neighbors() const {return data_->neighbors;}
const std::array<std::uint8_t,32>& OriginalFrontendPersonalSource::neighborhood_identity() const {return data_->identity;}
OriginalFrontendPersonalSource read_original_frontend_personal_source(const View& view){
    if(view.size()>64u*1024u*1024u)throw FormatError("original frontend NBRS source exceeds64MiB");
    auto data=std::make_shared<OriginalFrontendPersonalSource::Data>();
    data->owned.resize(static_cast<std::size_t>(view.size()));view.read(0,data->owned.data(),data->owned.size());
    data->identity=sha256(data->owned.data(),data->owned.size());
    std::istringstream input(data->owned,std::ios::binary);View copy(input,data->owned.size());
    bool found=false;iff_chunks(copy,[&](const Chunk& c){if(c.type=="NBRS"&&c.id==1){
        if(found)throw FormatError("original frontend source has ambiguousNBRS1");
        found=true;data->neighbors=read_nbrs(c.payload);
    }});
    if(!found)throw FormatError("original frontend source lacksNBRS1");
    // NativePersonalState authenticates immutable seed shape/uniqueness too.
    data->raw=data->neighbors;
    NativePersonalState checked(data->neighbors,data->identity);
    return OriginalFrontendPersonalSource(std::move(data));
}
namespace {
std::array<std::uint8_t,32> digest(const NativePersonalSnapshot& s){const auto b=encode_native_personal_snapshot(s);return sha256(b.data(),b.size());}
bool same(const NativePersonalSource& a,const NativePersonalSource& b){return a.body==b.body&&same_native_personal_identity(a.identity,b.identity);}
std::vector<NativePersonalSource> cohort(const OriginalFrontendPersonalSource& source,const World& world,const NativePersonalSourceResolver& resolve,const OriginalFrontendGuidLookup& lookup){
    if(!resolve)throw FormatError("original frontend personal resolver is absent");
    const auto snapshot=world.snapshot();
    std::map<std::uint32_t,std::vector<std::int16_t>> indices;
    for(const auto& row:source.neighbors().entries)if(row.record)indices[row.record->guid].push_back(row.record->neighbor_id);
    std::vector<NativePersonalSource> out;std::set<std::int16_t> selected;
    bool original_prefix=true;
    for(const auto& row:source.neighbors().entries){
        if(!row.record){original_prefix=false;continue;} //425b05 original prefix remains bounded by first NULL.
        const auto& seed=*row.record;
        if(!original_prefix&&!source.is_created_neighbor(seed.neighbor_id,seed.guid))continue;
        bool live_guid=false;for(const auto& e:snapshot.entities)if(!e.dead&&!e.ui_ghost&&e.kind==EntityKind::avatar&&e.definition&&e.definition->guid==seed.guid)live_guid=true;
        if(!live_guid)continue;
        const auto& candidates=indices.at(seed.guid);std::optional<std::int16_t> mapped;
        if(lookup)mapped=lookup(seed.guid);
        else if(source.catalog()){
            mapped=native_personal_catalog_lookup(*source.catalog(),seed.guid);
            if(!mapped&&candidates.size()==1&&source.is_created_neighbor(candidates.front(),seed.guid))mapped=candidates.front();
        }
        else if(candidates.size()==1)mapped=candidates.front();
        else throw FormatError("original frontend ambiguous GUID requires its authenticated original catalog index");
        if(!mapped)continue; //Original index lookup returned absent; no body is selected.
        if(std::find(candidates.begin(),candidates.end(),*mapped)==candidates.end())throw FormatError("original frontend GUID index selected a mismatched NBRS identity");
        const auto nid=*mapped;
        const Entity* body=nullptr;
        for(const auto& e:snapshot.entities){
            if(e.dead||e.ui_ghost||e.kind!=EntityKind::avatar||!e.definition)continue;
            if(e.definition->guid!=seed.guid||e.person_data.size()<=31||e.person_data[31]!=nid)continue;
            if(body)throw FormatError("original frontend live person lookup is ambiguous");body=&e;
        }
        if(!body)continue;
        // No implicit sorted/deduplicated writer cohort. An unresolved aliased
        // original GUID index must be admitted explicitly rather than guessed.
        if(!selected.insert(nid).second)throw FormatError("original frontend repeated GUID mapping requires exact index authority");
        auto actual=resolve(body->id);
        if(actual.body!=WorldEntityReference{body->id,body->generation}||actual.identity.neighbor_id!=nid||actual.identity.guid!=seed.guid)
            throw FormatError("original frontend resource resolver selected another exact person");
        out.push_back(std::move(actual));
    }
    const auto after=world.snapshot();
    if(after.generation_sequence!=snapshot.generation_sequence||after.entities.size()!=snapshot.entities.size())
        throw FormatError("original frontend resolver changed source lifetimes");
    for(const auto& e:snapshot.entities)if(e.kind==EntityKind::avatar){const auto* now=world.find(e.id);
        if(!now||now->generation!=e.generation||now->dead!=e.dead||now->person_data!=e.person_data)
            throw FormatError("original frontend resolver changed source personal data");
    }
    return out;
}
}
const NativePersonalSnapshot& PreparedOriginalFrontendPersonalSave::preview_snapshot() const {
    if(!owner_)throw FormatError("original frontend personal save was not prepared");return preview_;
}
PreparedOriginalFrontendPersonalSave prepare_original_frontend_personal_save(const OriginalFrontendPersonalSource& source,
    const World& world,const NativePersonalState& state,std::int16_t simless,const NativePersonalSourceResolver& resolve,const OriginalFrontendGuidLookup& lookup){
    if(simless!=original_frontend_playing_simless)throw FormatError("original frontend personal save requires playing Global32=0");
    if(source.neighborhood_identity()!=state.snapshot().neighborhood_identity)throw FormatError("original frontend personal source neighborhood differs");
    const auto admitted=state.snapshot().catalog?source.with_catalog(*state.snapshot().catalog):source;
    PreparedOriginalFrontendPersonalSave out;out.owner_=&state;out.source_=admitted;out.resolve_=resolve;out.lookup_=lookup;
    out.before_=digest(state.snapshot());out.sources_=cohort(admitted,world,resolve,lookup);
    if(out.sources_.empty())out.preview_=state.snapshot();
    else {out.prepared_=state.prepare(world,out.sources_,NativePersonalWriteSource::frontend_save);out.preview_=out.prepared_->preview_snapshot();}
    if(!out.source_current(world,state))throw FormatError("original frontend personal preparation lost its exact sources");
    return out;
}
bool PreparedOriginalFrontendPersonalSave::source_current(const World& world,const NativePersonalState& state) const {
    if(consumed_||owner_!=&state||!source_||before_!=digest(state.snapshot()))return false;
    try{const auto now=cohort(*source_,world,resolve_,lookup_);if(now.size()!=sources_.size())return false;
        for(std::size_t i=0;i<now.size();++i)if(!same(now[i],sources_[i]))return false;
        return before_==digest(state.snapshot())&&(!prepared_||state.source_current(*prepared_,world));
    }catch(const std::exception&){return false;}
}
bool PreparedOriginalFrontendPersonalSave::commit(NativePersonalState& state,const World& world){
    if(!source_current(world,state))return false;
    if(prepared_&&!state.commit(std::move(*prepared_),world))return false;
    consumed_=true;return true;
}
} // namespace odh
