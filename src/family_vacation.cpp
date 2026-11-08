// SPDX-License-Identifier: MPL-2.0
#include "odh/family_vacation.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <limits>
#include <set>
namespace odh {
namespace {
constexpr std::size_t max_rows=4096,max_bytes=2*1024*1024;
bool nonzero(const FamilyVacationDigest& d){return std::any_of(d.begin(),d.end(),[](auto v){return v!=0;});}
struct Writer {
    std::vector<std::uint8_t> out;
    void put(std::uint64_t n,unsigned count){if(out.size()+count>max_bytes)throw FormatError("vacation codec budget");for(unsigned i=0;i<count;++i)out.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
    void digest(const FamilyVacationDigest& d){out.insert(out.end(),d.begin(),d.end());}
    void bytes(const std::vector<std::uint8_t>& b){if(b.size()>max_bytes-out.size())throw FormatError("vacation codec budget");out.insert(out.end(),b.begin(),b.end());}
};
struct Reader {
    const std::vector<std::uint8_t>& in;std::size_t at=0;
    std::uint64_t get(unsigned n){if(n>in.size()-at)throw FormatError("truncated vacation codec");std::uint64_t v=0;for(unsigned i=0;i<n;++i)v|=std::uint64_t(in[at++])<<(8*i);return v;}
    FamilyVacationDigest digest(){FamilyVacationDigest d{};for(auto& v:d)v=static_cast<std::uint8_t>(get(1));return d;}
    std::vector<std::uint8_t> bytes(std::size_t n){if(n>in.size()-at)throw FormatError("vacation section extent");auto first=in.begin()+static_cast<std::ptrdiff_t>(at);at+=n;return {first,first+static_cast<std::ptrdiff_t>(n)};}
    void end(){if(at!=in.size())throw FormatError("vacation codec trailing bytes");}
};
std::int32_t signed32(std::uint32_t v){return static_cast<std::int32_t>(v<0x80000000U?std::int64_t(v):std::int64_t(v)-0x100000000LL);}
bool same_seed(const FamilyVacationRow& a,const FamilyVacationRow& b){return a.family_chunk_id==b.family_chunk_id&&a.family_identity==b.family_identity&&a.serialized_family_number==b.serialized_family_number&&a.fami_version==b.fami_version&&a.seed_word==b.seed_word;}
const FamilyVacationRow* row(const FamilyVacationSnapshot& s,std::uint16_t id){const auto i=std::find_if(s.families.begin(),s.families.end(),[&](const auto& v){return v.family_chunk_id==id;});return i==s.families.end()?nullptr:&*i;}
void valid_phase(const FamilyVacationPhase& p,const FamilyVacationSnapshot& s){
    const auto* f=row(s,p.family_chunk_id);
    if(!p.family_chunk_id||!f||!f->active||f->family_identity!=p.family_identity||!f->current_word||!nonzero(p.source_head)||!nonzero(p.before_bank))throw FormatError("vacation phase identity/source unavailable");
    if(p.kind==FamilyVacationPhaseKind::set_before_avatars){if(p.destination<40||p.destination>49||p.after_word!=p.destination)throw FormatError("vacation set phase range/value");}
    else if(p.kind==FamilyVacationPhaseKind::clear_residential_load){if(!p.destination||p.destination>98||p.after_word)throw FormatError("vacation clear phase range/value");}
    else throw FormatError("unknown vacation phase");
}
}
FamilyVacationRow family_vacation_seed(const FamilyEntry& entry,const FamilyVacationDigest& identity,bool active){
    FamilyVacationRow out;out.family_chunk_id=entry.chunk_id;out.family_identity=identity;out.serialized_family_number=entry.family.family_number;out.fami_version=entry.family.version;out.active=active;
    if(out.fami_version>9)throw FormatError("vacation FAMI version lacks source profile");
    if(out.fami_version>=8){const auto& f=entry.family;const auto need=out.fami_version==9?16U:4U;
        if(f.remaining_bytes!=f.tail_bytes.size()||f.tail_bytes.size()<need)throw FormatError("vacation FAMI suffix incomplete");
        std::uint32_t word=0;for(unsigned n=0;n<4;++n)word|=std::uint32_t(f.tail_bytes[n])<<(8*n);
        if(out.fami_version==9&&(!f.original_v9_extension||f.original_v9_extension->extension_dword!=word))throw FormatError("vacation typed FAMI9 seed differs from raw suffix");
        out.seed_word=word;out.current_word=word;
    }
    // Original 004100cf initializes +13c to zero before v<=7 deserialize,
    // whose 00410561 branch leaves it untouched. This is a runtime seed, not a suffix.
    else {out.seed_word=0;out.current_word=0;}
    return out;
}
void validate_family_vacation_snapshot(const FamilyVacationSnapshot& s){
    if(s.version!=1||!s.revision||!nonzero(s.neighborhood_identity)||s.families.size()>max_rows)throw FormatError("vacation snapshot version/identity/budget");
    std::set<std::uint16_t> ids;
    for(const auto& f:s.families){if(!ids.insert(f.family_chunk_id).second||!nonzero(f.family_identity)||f.fami_version>9||f.seed_word.has_value()!=f.current_word.has_value()||!f.seed_word.has_value()||(f.fami_version<8&&f.seed_word!=std::optional<std::uint32_t>{0}))throw FormatError("vacation family seed identity/profile");}
}
std::vector<std::uint8_t> encode_family_vacation_snapshot(const FamilyVacationSnapshot& s){
    validate_family_vacation_snapshot(s);Writer w;w.put(s.version,4);w.digest(s.neighborhood_identity);w.put(s.revision,8);w.put(s.families.size(),4);
    for(const auto& f:s.families){w.put(f.family_chunk_id,2);w.put((f.active?1U:0U)|(f.seed_word?2U:0U),2);w.put(static_cast<std::uint32_t>(f.serialized_family_number),4);w.put(f.fami_version,4);w.put(f.seed_word.value_or(0),4);w.put(f.current_word.value_or(0),4);w.digest(f.family_identity);}return w.out;
}
FamilyVacationSnapshot decode_family_vacation_snapshot(const std::vector<std::uint8_t>& bytes){
    if(bytes.size()>max_bytes)throw FormatError("vacation snapshot byte budget");Reader r{bytes};FamilyVacationSnapshot s;s.version=static_cast<std::uint32_t>(r.get(4));s.neighborhood_identity=r.digest();s.revision=r.get(8);const auto count=r.get(4);if(count>max_rows)throw FormatError("vacation family count");
    for(std::uint64_t n=0;n<count;++n){FamilyVacationRow f;f.family_chunk_id=static_cast<std::uint16_t>(r.get(2));const auto flags=r.get(2);if(flags>3)throw FormatError("vacation family flags");f.active=(flags&1)!=0;f.serialized_family_number=signed32(static_cast<std::uint32_t>(r.get(4)));f.fami_version=static_cast<std::uint32_t>(r.get(4));const auto seed=static_cast<std::uint32_t>(r.get(4)),word=static_cast<std::uint32_t>(r.get(4));if(flags&2){f.seed_word=seed;f.current_word=word;}else if(seed||word)throw FormatError("absent vacation seed contains bytes");f.family_identity=r.digest();s.families.push_back(std::move(f));}r.end();validate_family_vacation_snapshot(s);return s;
}
FamilyVacationDigest family_vacation_digest(const FamilyVacationSnapshot& s){const auto b=encode_family_vacation_snapshot(s);return sha256(b.data(),b.size());}
void validate_family_vacation_successor(const FamilyVacationSnapshot& old,const FamilyVacationSnapshot& next){
    validate_family_vacation_snapshot(old);validate_family_vacation_snapshot(next);if(old.neighborhood_identity!=next.neighborhood_identity||next.revision<old.revision)throw FormatError("vacation successor identity/revision");
    for(const auto& f:old.families){const auto* n=row(next,f.family_chunk_id);if(!n||!same_seed(f,*n)||( !f.active&&n->active))throw FormatError("vacation successor rebinds historical family");}
    if(next.revision==old.revision&&encode_family_vacation_snapshot(old)!=encode_family_vacation_snapshot(next))throw FormatError("vacation changed state without revision");
}
std::optional<std::int16_t> family_vacation_available_count(const FamilyVacationSnapshot& s){validate_family_vacation_snapshot(s);std::set<std::uint32_t> occupied;for(const auto& f:s.families)if(f.active){if(!f.current_word)return {};if(*f.current_word>=40&&*f.current_word<=48)occupied.insert(*f.current_word);}return static_cast<std::int16_t>(9-occupied.size());}
std::vector<FamilyEntry> project_family_vacation_families(const std::vector<FamilyEntry>& source,const FamilyVacationSnapshot& s){
    validate_family_vacation_snapshot(s);auto result=source;for(auto& f:result){const auto* v=row(s,f.chunk_id);if(!v||!v->active||v->serialized_family_number!=f.family.family_number||v->fami_version!=f.family.version)throw FormatError("vacation family projection identity differs");
        if(!v->current_word)continue;const auto seed=family_vacation_seed(f,v->family_identity);if(seed.seed_word!=v->seed_word&&seed.seed_word!=v->current_word)throw FormatError("vacation family projection seed differs");
        if(f.family.version<8)continue; // Preserve exact historical payload; bank owns runtime +13c.
        for(unsigned n=0;n<4;++n)f.family.tail_bytes[n]=static_cast<std::uint8_t>(*v->current_word>>(8*n));if(f.family.original_v9_extension)f.family.original_v9_extension->extension_dword=*v->current_word;
    }return result;
}
void validate_family_vacation_overlay_state(const FamilyVacationOverlayState& value){
    validate_family_vacation_snapshot(value.state);if(value.private_heads.size()>max_rows||value.phases.size()>max_rows)throw FormatError("vacation overlay metadata budget");std::set<std::uint16_t> ids;
    for(const auto& h:value.private_heads){const auto* f=row(value.state,h.family_chunk_id);if(!ids.insert(h.family_chunk_id).second||!f||!f->active||h.family_identity!=f->family_identity||h.serialized_family_number!=f->serialized_family_number||!h.home_house||h.home_house>98||(!h.home_mode||h.home_mode==6||h.home_mode>7)||!nonzero(h.checkpoint_sha256))throw FormatError("vacation private head identity/namespace");}ids.clear();
    for(const auto& p:value.phases){if(!ids.insert(p.family_chunk_id).second)throw FormatError("duplicate vacation phase");valid_phase(p,value.state);if(row(value.state,p.family_chunk_id)->current_word!=std::optional<std::uint32_t>{p.after_word})throw FormatError("vacation phase does not name current family value");}
}
std::vector<std::uint8_t> encode_family_vacation_overlay_state(const FamilyVacationOverlayState& value){
    validate_family_vacation_overlay_state(value);Writer w;w.put(1,4);const auto snapshot=encode_family_vacation_snapshot(value.state);w.put(snapshot.size(),4);w.bytes(snapshot);w.put(value.private_heads.size(),4);
    for(const auto& h:value.private_heads){w.put(h.family_chunk_id,2);w.put(h.home_house,2);w.put(h.home_mode,1);w.put(0,3);w.put(static_cast<std::uint32_t>(h.serialized_family_number),4);w.digest(h.family_identity);w.digest(h.checkpoint_sha256);}w.put(value.phases.size(),4);
    for(const auto& p:value.phases){w.put(static_cast<std::uint32_t>(p.kind),4);w.put(p.family_chunk_id,2);w.put(p.destination,2);w.digest(p.family_identity);w.digest(p.source_head);w.digest(p.before_bank);w.put(p.before_word,4);w.put(p.after_word,4);}return w.out;
}
FamilyVacationOverlayState decode_family_vacation_overlay_state(const std::vector<std::uint8_t>& bytes){
    if(bytes.size()>max_bytes)throw FormatError("vacation overlay byte budget");Reader r{bytes};if(r.get(4)!=1)throw FormatError("vacation overlay version");FamilyVacationOverlayState out;out.state=decode_family_vacation_snapshot(r.bytes(static_cast<std::size_t>(r.get(4))));const auto heads=r.get(4);if(heads>max_rows)throw FormatError("vacation head count");for(std::uint64_t n=0;n<heads;++n){FamilyVacationPrivateHead h;h.family_chunk_id=static_cast<std::uint16_t>(r.get(2));h.home_house=static_cast<std::uint16_t>(r.get(2));h.home_mode=static_cast<std::uint8_t>(r.get(1));if(r.get(3))throw FormatError("vacation head reserved bytes");h.serialized_family_number=signed32(static_cast<std::uint32_t>(r.get(4)));h.family_identity=r.digest();h.checkpoint_sha256=r.digest();out.private_heads.push_back(h);}const auto phases=r.get(4);if(phases>max_rows)throw FormatError("vacation phase count");for(std::uint64_t n=0;n<phases;++n){FamilyVacationPhase p;p.kind=static_cast<FamilyVacationPhaseKind>(r.get(4));p.family_chunk_id=static_cast<std::uint16_t>(r.get(2));p.destination=static_cast<std::uint16_t>(r.get(2));p.family_identity=r.digest();p.source_head=r.digest();p.before_bank=r.digest();p.before_word=static_cast<std::uint32_t>(r.get(4));p.after_word=static_cast<std::uint32_t>(r.get(4));out.phases.push_back(p);}r.end();validate_family_vacation_overlay_state(out);return out;
}
const FamilyVacationPrivateHead* family_vacation_private_head(const FamilyVacationOverlayState& v,std::uint16_t id){const auto i=std::find_if(v.private_heads.begin(),v.private_heads.end(),[&](const auto& h){return h.family_chunk_id==id;});return i==v.private_heads.end()?nullptr:&*i;}
const FamilyVacationPhase* family_vacation_phase(const FamilyVacationOverlayState& v,std::uint16_t id){const auto i=std::find_if(v.phases.begin(),v.phases.end(),[&](const auto& p){return p.family_chunk_id==id;});return i==v.phases.end()?nullptr:&*i;}
void set_family_vacation_private_head(FamilyVacationOverlayState& v,const FamilyVacationPrivateHead& h){auto i=std::find_if(v.private_heads.begin(),v.private_heads.end(),[&](const auto& p){return p.family_chunk_id==h.family_chunk_id;});if(i==v.private_heads.end())v.private_heads.push_back(h);else *i=h;}
void set_family_vacation_phase(FamilyVacationOverlayState& v,const FamilyVacationPhase& p){auto i=std::find_if(v.phases.begin(),v.phases.end(),[&](const auto& x){return x.family_chunk_id==p.family_chunk_id;});if(i==v.phases.end())v.phases.push_back(p);else *i=p;}
void clear_family_vacation_phase(FamilyVacationOverlayState& v,std::uint16_t id){v.phases.erase(std::remove_if(v.phases.begin(),v.phases.end(),[&](const auto& p){return p.family_chunk_id==id;}),v.phases.end());}
bool family_vacation_phase_matches(const FamilyVacationOverlayState& v,const FamilyVacationPhase& phase) noexcept {try{validate_family_vacation_overlay_state(v);const auto* p=family_vacation_phase(v,phase.family_chunk_id);return p&&p->kind==phase.kind&&p->destination==phase.destination&&p->family_identity==phase.family_identity&&p->source_head==phase.source_head&&p->before_bank==phase.before_bank&&p->before_word==phase.before_word&&p->after_word==phase.after_word;}catch(...){return false;}}
FamilyVacationState::FamilyVacationState(std::vector<FamilyVacationRow> seeds,FamilyVacationDigest neighborhood){seed_.neighborhood_identity=neighborhood;seed_.families=std::move(seeds);validate_family_vacation_snapshot(seed_);state_=seed_;}
void FamilyVacationState::restore(const FamilyVacationSnapshot& s){validate_family_vacation_snapshot(s);if(s.neighborhood_identity!=seed_.neighborhood_identity||s.families.size()!=seed_.families.size())throw FormatError("vacation restore source set differs");for(const auto& f:seed_.families){const auto* actual=row(s,f.family_chunk_id);if(!actual||!same_seed(f,*actual)||f.active!=actual->active)throw FormatError("vacation restore rebinds seed or family lifetime");}state_=s;}
std::optional<std::int16_t> FamilyVacationState::available_count() const{return family_vacation_available_count(state_);}
PreparedFamilyVacationChange FamilyVacationState::prepare(const FamilyVacationPhase& phase) const {
    valid_phase(phase,state_);const auto* f=row(state_,phase.family_chunk_id);if(phase.before_bank!=family_vacation_digest(state_)||f->current_word!=std::optional<std::uint32_t>{phase.before_word})throw FormatError("vacation prepare source bank/value changed");PreparedFamilyVacationChange out;out.owner_=this;out.before_=phase.before_bank;out.preview_=state_;out.phase_=phase;
    if(phase.before_word!=phase.after_word){if(out.preview_.revision==std::numeric_limits<std::uint64_t>::max())throw FormatError("vacation revision exhausted");++out.preview_.revision;for(auto& v:out.preview_.families)if(v.family_chunk_id==phase.family_chunk_id)v.current_word=phase.after_word;out.changed_=true;}return out;
}
bool FamilyVacationState::source_current(const PreparedFamilyVacationChange& p) const {return p.owner_==this&&!p.consumed_&&p.before_==family_vacation_digest(state_);}
bool FamilyVacationState::commit(PreparedFamilyVacationChange& p){if(!source_current(p))return false;state_=p.preview_;p.consumed_=true;return true;}
const FamilyVacationSnapshot& PreparedFamilyVacationChange::preview_snapshot() const{if(!owner_)throw FormatError("vacation change not prepared");return preview_;}
const FamilyVacationPhase& PreparedFamilyVacationChange::phase() const{if(!owner_)throw FormatError("vacation change not prepared");return phase_;}
} // namespace odh
