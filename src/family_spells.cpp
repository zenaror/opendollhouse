// SPDX-License-Identifier: MPL-2.0
#include "odh/family_spells.hpp"
#include "odh/original_lot_classification.hpp"
#include <algorithm>
#include <utility>

namespace odh {
namespace {
constexpr std::uint32_t controller_guid=0xb6c90029U;
FamilySpellsOutcome unsupported(std::string reason) {return {false,false,std::move(reason)};}
FamilySpellsOutcome noop() {return {true,false,{}};}
std::int16_t signed16(std::uint16_t bits) {
    return static_cast<std::int16_t>(bits<32768?static_cast<std::int32_t>(bits):static_cast<std::int32_t>(bits)-65536);
}
// Distinguish a known guard failure from unavailable runtime authority.
struct Guard {FamilySpellsOutcome outcome;bool pass=false;};
Guard guards(const FamilySpellsSnapshot& state,bool capture,const LotZoning* zoning,
    const FamilySpellsGlobals& globals) {
    if(!capture&&!state.loaded_presence)return {noop(),false};
    if(!capture&&!state.family_chunk_id)return {noop(),false};
    if(!globals)return {unsupported("family spells global authority unavailable"),false};
    // Reverse source tests Global10 first; forward source checks Global9 first.
    std::optional<std::int16_t> house;
    if(capture) {
        house=globals(10);
        if(!house)return {unsupported("family spells Global10 unavailable"),false};
        if(!*house)return {noop(),false};
        if(!state.family_chunk_id)return {noop(),false};
    }
    const auto active=globals(9);
    if(!active)return {unsupported("family spells Global9 unavailable"),false};
    if(static_cast<std::int32_t>(*active)!=static_cast<std::int32_t>(state.family_chunk_id))return {noop(),false};
    if(!house)house=globals(10);
    if(!house)return {unsupported("family spells Global10 unavailable"),false};
    // Original classification key0 is always1, without touching the row bank.
    if(!*house)return {noop(),true};
    if(!zoning)return {unsupported("family spells explicit zoning authority unavailable"),false};
    const auto classification=classify_original_lot(zoning,*house);
    if(!classification)return {unsupported("family spells zoning row is outside residential/community profile"),false};
    return {noop(),*classification==OriginalLotClass::residential};
}
struct Target {const Entity* entity=nullptr;FamilySpellsOutcome outcome=noop();};
Target controller(const World& world) {
    const auto id=world.next_entity_of_guid(-32768,controller_guid);
    if(!id)return {};
    if(world.next_entity_of_guid(*id,controller_guid))
        return {nullptr,unsupported("family spells controller source order is unavailable for duplicate GUIDs")};
    const auto* entity=world.find(*id);
    if(!entity||entity->attributes.size()<6)
        return {nullptr,unsupported("family spells controller requires six existing Attributes")};
    return {entity,noop()};
}
}
void validate_family_spells_snapshot(const FamilySpellsSnapshot& value) {
    if(value.version!=1)throw FormatError("unsupported family spells snapshot version");
}
FamilySpells::FamilySpells(const FamilyEntry& entry) {
    state_.family_chunk_id=entry.chunk_id;
    state_.family_number=entry.family.family_number;
    if(entry.family.version>9)throw FormatError("family spells requires a proven original FAMI version");
    if(entry.family.version==8&&entry.family.tail_bytes.size()>=4) {
        for(std::size_t n=0;n<4;++n)state_.extension_dword|=std::uint32_t{entry.family.tail_bytes[n]}<<(8*n);
    }
    if(entry.family.version==9) {
        if(!entry.family.original_v9_extension||entry.family.remaining_bytes<16||
           entry.family.remaining_bytes!=entry.family.tail_bytes.size())
            throw FormatError("family spells FAMI9 has no complete original extension");
        state_.extension_dword=entry.family.original_v9_extension->extension_dword;
        state_.words=entry.family.original_v9_extension->spell_words;
        state_.loaded_presence=true;
    }
    seed_extension_dword_=state_.extension_dword;
}
void FamilySpells::restore(const FamilySpellsSnapshot& value) {
    validate_family_spells_snapshot(value);
    if(value.family_chunk_id!=state_.family_chunk_id||value.family_number!=state_.family_number)
        throw FormatError("family spells snapshot identity differs from private carrier");
    if(value.loaded_presence!=state_.loaded_presence||value.extension_dword!=state_.extension_dword)
        throw FormatError("family spells snapshot differs from immutable loaded metadata");
    state_=value;
}
std::vector<std::uint8_t> encode_family_spells_snapshot(const FamilySpellsSnapshot& value) {
    validate_family_spells_snapshot(value);
    std::vector<std::uint8_t> out;
    out.reserve(28);
    const auto put=[&](std::uint32_t word,std::size_t bytes){
        for(std::size_t n=0;n<bytes;++n)out.push_back(static_cast<std::uint8_t>(word>>(8*n)));
    };
    put(value.version,4);put(value.family_chunk_id,2);put(value.loaded_presence?1U:0U,1);put(0,1);
    put(static_cast<std::uint32_t>(value.family_number),4);put(value.extension_dword,4);
    for(const auto word:value.words)put(static_cast<std::uint16_t>(word),2);
    return out;
}
FamilySpellsSnapshot decode_family_spells_snapshot(const std::vector<std::uint8_t>& bytes) {
    if(bytes.size()!=28)throw FormatError("family spells snapshot requires exactly28 bytes");
    const auto get=[&](std::size_t at,std::size_t count){
        std::uint32_t value=0;
        for(std::size_t n=0;n<count;++n)value|=std::uint32_t{bytes[at+n]}<<(8*n);
        return value;
    };
    if(bytes[6]>1||bytes[7]!=0)throw FormatError("invalid family spells snapshot flags");
    FamilySpellsSnapshot value;
    value.version=get(0,4);value.family_chunk_id=static_cast<std::uint16_t>(get(4,2));
    value.loaded_presence=bytes[6]!=0;
    const auto number=get(8,4);
    value.family_number=static_cast<std::int32_t>(number<0x80000000U?static_cast<std::int64_t>(number):static_cast<std::int64_t>(number)-0x100000000LL);
    value.extension_dword=get(12,4);
    for(std::size_t n=0;n<6;++n)value.words[n]=signed16(static_cast<std::uint16_t>(get(16+2*n,2)));
    validate_family_spells_snapshot(value);
    return value;
}
FamilySpellsOutcome apply_family_spells(World& world,FamilySpells* state,const LotZoning* zoning,
    const FamilySpellsGlobals& globals) {
    if(!state)return unsupported("family spells private carrier unavailable");
    const auto saved=state->snapshot();
    const auto guard=guards(saved,false,zoning,globals);
    if(!guard.outcome.supported||!guard.pass)return guard.outcome;
    const auto target=controller(world);
    if(!target.outcome.supported||!target.entity)return target.outcome;
    const WorldEntityReference reference{target.entity->id,target.entity->generation};
    bool changed=false;
    for(std::size_t n=0;n<6;++n)changed|=target.entity->attributes[n]<saved.words[n];
    // World validates first and notifies observers only after the entire bank
    // prefix is committed. Observers may then remove the entity safely.
    if(!world.merge_attributes_max(reference,saved.words.data(),saved.words.size()))
        return unsupported("family spells attribute publication is unavailable");
    return {true,changed,{}};
}
VmPrimitive family_spells_primitive(World& world,FamilySpells* state,const LotZoning* zoning,
    FamilySpellsGlobals globals) {
    return [&world,state,zoning,globals=std::move(globals)](const VmFrame& frame,const Instruction& instruction,
        const Ts1Temps&,const VmDispatch& dispatch)->PrimitiveOutcome {
        if(instruction.opcode!=1||instruction.operands[0]!=43)return {};
        if(!dispatch.origin||!dispatch.origin->matches_active_query(frame,instruction,dispatch))
            return {PrimitiveDisposition::unsupported,"family spells requires its actual VM dispatch"};
        const auto outcome=apply_family_spells(world,state,zoning,globals);
        return outcome.supported?PrimitiveOutcome{PrimitiveDisposition::goto_true,{}}:
            PrimitiveOutcome{PrimitiveDisposition::unsupported,outcome.diagnostic};
    };
}
FamilySpellsOutcome capture_family_spells(const World& world,FamilySpells& state,
    const LotZoning* zoning,const FamilySpellsGlobals& globals) {
    auto candidate=state.snapshot();
    const auto guard=guards(candidate,true,zoning,globals);
    if(!guard.outcome.supported||!guard.pass)return guard.outcome;
    const auto target=controller(world);
    if(!target.outcome.supported||!target.entity)return target.outcome;
    bool changed=false;
    for(std::size_t n=0;n<6;++n) {
        const auto word=std::max(candidate.words[n],target.entity->attributes[n]);
        changed|=word!=candidate.words[n];candidate.words[n]=word;
    }
    state.restore(candidate);
    return {true,changed,{}};
}
bool project_family_spells_vacation(FamilySpellsSnapshot& value,const FamilyVacationSnapshot& bank,
        const FamilyVacationPhase* phase){
    validate_family_spells_snapshot(value);validate_family_vacation_snapshot(bank);
    const auto i=std::find_if(bank.families.begin(),bank.families.end(),[&](const auto& f){return f.family_chunk_id==value.family_chunk_id;});
    if(i==bank.families.end()||!i->active||i->serialized_family_number!=value.family_number||!i->seed_word||!i->current_word||value.loaded_presence!=(i->fami_version==9))
        throw FormatError("family spells vacation provenance/profile differs");
    const bool prior_phase=phase&&phase->family_chunk_id==i->family_chunk_id&&phase->family_identity==i->family_identity&&phase->after_word==*i->current_word&&value.extension_dword==phase->before_word;
    if(value.extension_dword!=*i->seed_word&&value.extension_dword!=*i->current_word&&!prior_phase)
        throw FormatError("family spells vacation value lacks seed/current/phase authority");
    const bool changed=value.extension_dword!=*i->current_word;value.extension_dword=*i->current_word;return changed;
}
bool project_family_spells_vacation_successor(FamilySpellsSnapshot& value,
        const FamilyVacationSnapshot& before,const FamilyVacationSnapshot& after){
    validate_family_spells_snapshot(value);validate_family_vacation_successor(before,after);
    const auto prior=std::find_if(before.families.begin(),before.families.end(),[&](const auto& f){return f.family_chunk_id==value.family_chunk_id;});
    if(prior==before.families.end()||!prior->active||prior->serialized_family_number!=value.family_number||
       !prior->current_word||value.loaded_presence!=(prior->fami_version==9)||value.extension_dword!=*prior->current_word)
        throw FormatError("saved family spells do not match before vacation authority");
    const auto next=std::find_if(after.families.begin(),after.families.end(),[&](const auto& f){return f.family_chunk_id==value.family_chunk_id;});
    if(next==after.families.end()||!next->active||next->family_identity!=prior->family_identity||!next->current_word)
        throw FormatError("saved family spells lack current vacation family authority");
    const bool changed=value.extension_dword!=*next->current_word;value.extension_dword=*next->current_word;return changed;
}
void FamilySpells::admit_vacation(const FamilyVacationSnapshot& bank,const FamilyVacationPhase* phase){
    const auto i=std::find_if(bank.families.begin(),bank.families.end(),[&](const auto& f){return f.family_chunk_id==state_.family_chunk_id;});
    if(i==bank.families.end()||i->seed_word!=std::optional<std::uint32_t>{seed_extension_dword_})throw FormatError("family spells vacation immutable seed differs");
    (void)project_family_spells_vacation(state_,bank,phase);
}

} // namespace odh
