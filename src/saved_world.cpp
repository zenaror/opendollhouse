#include "odh/saved_world.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace odh {
namespace {
void check(bool ok, const char* why) { if (!ok) throw FormatError(why); }
void consume(std::uint64_t& available, std::uint64_t count, const char* why) {
    check(count<=available,why); available-=count;
}
bool same_definition(const ObjectDefinition& a,const ObjectDefinition& b) {
    return a.version==b.version && a.guid==b.guid && a.object_type==b.object_type &&
        a.master_id==b.master_id && a.sub_index==b.sub_index && a.is_multitile==b.is_multitile &&
        a.header_extent==b.header_extent && a.remaining_bytes==b.remaining_bytes;
}
template<class T> void count_matches(std::int64_t count,const std::vector<T>& values) {
    check(count>=0 && static_cast<std::uint64_t>(count)==values.size(),"saved world serialized count mismatch");
}
struct Budget {
    std::uint64_t words, strings, suffix;
    void word(std::uint64_t n) { consume(words,n,"saved world retained word budget exceeded"); }
    void string(const ObjmSavedString& s) {
        word(2+(s.padding?1:0)); // String field, original byte offset, optional padding.
        consume(strings,s.bytes.size(),"saved world retained string budget exceeded");
    }
};
void account(const SavedWorldEntity& r,Budget& b) {
    const auto& c=r.common; const auto& t=r.tail;
    b.word(4+3+1+1+8+68+5+r.saved.prefix.attributes.size());
    b.word(16); // Prefix cursors, saved ID/type/GUID, definition and OBJT metadata.
    b.word(12); // Common counts, flags, cursors and accounting metadata.
    for(const auto& f:c.stack) { b.word(7); b.word(f.parameters.size()); b.word(f.locals.size()); }
    for(const auto& rel:c.relationships) { b.word(3); b.word(rel.values.size()); }
    if(c.linked_slot) b.word(2);
    for(const auto& s:c.slots) { (void)s; b.word(2); }
    b.word(c.dynamic_sprite_flags.size());
    if(t.multitile) b.word(7);
    if(t.portal_count) b.word(1);
    if(t.portal_float_bits) b.word(t.portal_float_bits->size());
    b.word(1); // UnknownInt.
    b.word(7); // Tail cursors and accounting metadata.
    consume(b.suffix,t.remaining_data.size(),"saved world retained opaque suffix budget exceeded");
    if(!t.person) return;
    const auto& p=*t.person;
    b.word(8+9+16+16+p.person_data.size()+28);
    for(const auto* s:{&p.body,&p.body_texture,&p.unknown1,&p.unknown2,&p.left_hand,
        &p.left_hand_texture,&p.right_hand,&p.right_hand_texture,&p.head,&p.head_texture,
        &p.animation,&p.carry_animation,&p.base_animation}) b.string(*s);
    for(const auto& a:p.accessories) { b.string(a.name); b.string(a.binding); }
    for(const auto& i:p.interaction_queue) { (void)i; b.word(14); }
    for(const auto& u:p.object_uses) { (void)u; b.word(3); }
    for(const auto& d:p.motive_deltas) { (void)d; b.word(3); }
}
}

std::int16_t saved_motive_word(std::uint32_t bits) {
    const bool negative=(bits>>31)!=0;
    const unsigned exponent=(bits>>23)&255U;
    check(exponent!=255,"saved motive is not finite");
    // Every subnormal, and every normal value with exponent below -1, rounds
    // to zero. Exactly +/-0.5 also ties to even zero.
    if(exponent<126) return 0;
    const std::uint64_t mantissa=(bits&0x7fffffU)|0x800000U;
    check(exponent<=142,"saved motive exceeds signed16 range");
    const unsigned shift=150-exponent;
    auto magnitude=mantissa>>shift;
    const auto remainder=mantissa&((std::uint64_t{1}<<shift)-1);
    const auto half=std::uint64_t{1}<<(shift-1);
    if(remainder>half || (remainder==half && (magnitude&1))) ++magnitude;
    check(magnitude<=(negative?32768U:32767U),"saved motive exceeds signed16 range");
    const auto signed_value=negative?-static_cast<std::int32_t>(magnitude):static_cast<std::int32_t>(magnitude);
    return static_cast<std::int16_t>(signed_value);
}

std::unique_ptr<SavedWorld> make_saved_world(std::vector<SavedWorldEntity> source,
    const DefinitionIndex& definitions,const NeighborContext* neighbors,const SavedWorldLimits& limits) {
    check(source.size()<=limits.world.entities && source.size()<=32767,"saved world entity budget exceeded");
    Budget budget{limits.retained_words,limits.retained_string_bytes,limits.retained_suffix_bytes};
    std::map<std::int16_t,const SavedWorldEntity*> records;
    std::map<std::uint16_t,std::pair<std::uint32_t,std::uint16_t>> types;
    std::set<std::uint32_t> block_offsets;
    std::map<std::int16_t,std::optional<std::uint32_t>> masters;
    std::map<std::int16_t,std::vector<std::int16_t>> motives;
    // Validate budgets from actual owned vectors, never the caller's reported
    // decoded_words/string_bytes. All retained data is bounded before copying.
    for(const auto& r:source) account(r,budget);
    for(const auto& r:source) {
        const auto id=r.saved.object_id;
        check(id>0 && id<=32767 && records.emplace(static_cast<std::int16_t>(id),&r).second,
              "saved world invalid or duplicate entity ID");
        check(r.saved.type_id!=0 && r.saved.guid!=0 && r.saved.prefix.object_data[11]==id,
              "saved world ID/type mapping mismatch");
        const auto mapping=std::make_pair(r.saved.guid,r.serialized_object_type);
        const auto [type,inserted]=types.emplace(r.saved.type_id,mapping);
        check(inserted || type->second==mapping,"saved world inconsistent OBJT type mapping");
        check(block_offsets.insert(r.saved.prefix.block_offset).second,"saved world duplicate decoded block offset");
        const auto* candidate=definitions.unique(r.saved.guid);
        check(candidate && same_definition(candidate->definition,r.definition),
              "saved world definition unavailable, ambiguous or mismatched");
        check((r.definition.version==136 || r.definition.version==138) &&
            r.definition.is_multitile==(r.definition.master_id!=0) &&
            r.tail.multitile.has_value()==r.definition.is_multitile,"saved world multitile metadata mismatch");
        check(r.tail.person.has_value()==(r.serialized_object_type==2) &&
            r.tail.portal_count.has_value()==(r.serialized_object_type==8),
            "saved world serialized object type/tail branch mismatch");
        const auto* master=r.definition.master_id?definitions.unique_master(*candidate):nullptr;
        masters.emplace(static_cast<std::int16_t>(id),master?std::optional<std::uint32_t>{master->definition.guid}:std::nullopt);
        const auto& p=r.saved.prefix; const auto& c=r.common; const auto& t=r.tail;
        check(p.level==1 || p.level==2,"saved world invalid saved level");
        check(p.object_data[1]>=0 && p.object_data[1]<=7,"saved world invalid direction");
        const bool outside=p.x==-16 && p.y==-16;
        check(outside || (p.x>=0 && p.y>=0 && p.x<=32767 && p.y<=32767),"saved world invalid coordinates/sentinel");
        check(c.block_offset==p.block_offset && t.block_offset==p.block_offset &&
            c.odd_offset==p.odd_offset && t.odd_offset==p.odd_offset &&
            c.start_bits==p.consumed_bits && t.start_bits==c.consumed_bits &&
            c.consumed_bits>c.start_bits && t.consumed_bits>t.start_bits &&
            c.consumed_bits-c.start_bits<=p.remaining_bits &&
            c.remaining_bits==p.remaining_bits-(c.consumed_bits-c.start_bits) &&
            t.consumed_bits-t.start_bits<=c.remaining_bits &&
            t.remaining_bits==c.remaining_bits-(t.consumed_bits-t.start_bits),
            "saved world decoded record cursor mismatch");
        check(t.remaining_bits/8+(t.remaining_bits%8!=0)==t.remaining_data.size(),"saved world opaque suffix extent mismatch");
        if(t.remaining_bits%8 && !t.remaining_data.empty())
            check((t.remaining_data.back()&((1U<<(8-t.remaining_bits%8))-1))==0,"saved world opaque suffix low bits not zero");
        count_matches(c.stack_count,c.stack); count_matches(c.relationship_count,c.relationships);
        check(c.relationship_flag<0,"saved world unsupported relationship flag");
        check(c.slot_count>=0 && static_cast<std::uint64_t>(c.slot_count)==c.slots.size()+(c.linked_slot?1:0) &&
            c.linked_slot.has_value()==(c.slot_count!=0),"saved world slot count mismatch");
        count_matches(c.dynamic_sprite_flag_count,c.dynamic_sprite_flags);
        for(const auto& f:c.stack) {
            count_matches(f.parameter_count,f.parameters); count_matches(f.local_count,f.locals);
        }
        for(const auto& rel:c.relationships) {
            check(rel.is_present!=0,"saved world absent relationship record"); count_matches(rel.value_count,rel.values);
        }
        check(t.portal_count.has_value()==t.portal_float_bits.has_value(),"saved world portal count mismatch");
        if(t.portal_count) count_matches(*t.portal_count,*t.portal_float_bits);
        if(t.person) {
            const auto& person=*t.person;
            check(person.person_data.size()==80 || person.person_data.size()==256,"saved world unsupported PersonData extent");
            count_matches(person.accessory_count,person.accessories);
            count_matches(person.interaction_queue_count,person.interaction_queue);
            count_matches(person.object_use_count,person.object_uses);
            count_matches(person.motive_delta_count,person.motive_deltas);
            std::vector<std::int16_t> adapted; adapted.reserve(16);
            for(const auto bits:person.motive_data_bits) adapted.push_back(saved_motive_word(bits));
            motives.emplace(static_cast<std::int16_t>(id),std::move(adapted));
        }
    }
    auto reference=[&](std::int32_t id,bool null_allowed=true) {
        check((null_allowed && id==0) || (id>0 && id<=32767 && records.count(static_cast<std::int16_t>(id))),
              "saved world dangling or invalid entity reference");
    };
    std::map<std::int16_t,std::pair<std::int16_t,std::size_t>> inbound;
    std::map<std::int16_t,SavedMultitileGroup> groups;
    for(const auto& r:source) {
        const auto id=static_cast<std::int16_t>(r.saved.object_id);
        const auto& c=r.common;
        if(c.linked_slot) reference(c.linked_slot->object_id);
        for(std::size_t slot=0;slot<c.slots.size();++slot) {
            const auto child=c.slots[slot].object_id; reference(child);
            if(child) check(child!=id && inbound.emplace(child,std::make_pair(id,slot)).second,
                            "saved world duplicate or self-contained child");
        }
        for(const auto& f:c.stack) reference(f.stack_object_id);
        for(const auto& rel:c.relationships) reference(rel.target_id,false);
        if(r.tail.person) {
            const auto& p=*r.tail.person;
            auto interaction=[&](const ObjmSavedInteraction& i) {
                check(i.unknown_zero==0,"saved world interaction prefix is nonzero");
                reference(i.caller_id); reference(i.target_id);
            };
            interaction(p.active_interaction); interaction(p.last_interaction);
            for(const auto& i:p.interaction_queue) interaction(i);
            for(const auto& u:p.object_uses) { reference(u.target_id); check(u.stack_length>=0,"saved world negative object-use stack length"); }
        }
        if(r.tail.multitile) {
            const auto parent=r.tail.multitile->parent_id; reference(parent);
            const auto leader=parent?parent:id;
            const auto& lead=*records.at(leader);
            check(lead.tail.multitile && !lead.tail.multitile->parent_id &&
                lead.definition.master_id==r.definition.master_id && masters.at(leader)==masters.at(id),
                "saved world invalid multitile leader/group");
            const auto* a=definitions.unique(r.saved.guid);const auto* z=definitions.unique(lead.saved.guid);
            check(a->source==z->source && a->asset==z->asset,"saved world multitile group crosses resources");
            auto [it,inserted]=groups.emplace(leader,SavedMultitileGroup{leader,masters.at(id),{}});
            (void)inserted; it->second.members.push_back(id);
        }
    }
    for(const auto& r:source) {
        const auto id=static_cast<std::int16_t>(r.saved.object_id);
        const auto container=r.saved.prefix.object_data[2],slot=r.saved.prefix.object_data[3];
        reference(container);
        const auto in=inbound.find(id);
        check(container==0 ? (slot==-1 && in==inbound.end()) :
            (slot>=0 && in!=inbound.end() && in->second.first==container && in->second.second==static_cast<std::size_t>(slot)),
            "saved world containment reciprocity mismatch");
        std::set<std::int16_t> visited;
        auto current=id;
        while(current) {
            check(visited.insert(current).second,"saved world containment cycle");
            current=records.at(current)->saved.prefix.object_data[2];
        }
    }
    auto result=std::make_unique<SavedWorld>();
    result->world=std::make_unique<World>(limits.world,AttributePolicy::ts1_bounded);
    result->relationships=neighbors?std::make_unique<RelationshipState>(*neighbors,limits.relationships):
        std::make_unique<RelationshipState>(limits.relationships);
    for(auto& item:groups) {
        auto& g=item.second;
        const auto leader=std::find(g.members.begin(),g.members.end(),g.leader);
        check(leader!=g.members.end(),"saved world missing multitile leader");
        std::rotate(g.members.begin(),leader,leader+1);
        result->multitile_groups.push_back(std::move(g));
    }
    const std::vector<std::int16_t> empty;
    std::vector<SavedObjectRelationships> relationships;
    relationships.reserve(source.size());
    for(const auto& r:source) {
        const auto id=static_cast<std::int16_t>(r.saved.object_id);const auto& p=r.saved.prefix;
        check(result->world->create_saved_entity(id,r.tail.person?EntityKind::avatar:EntityKind::object,r.saved.guid,
            p.attributes,r.tail.person?r.tail.person->person_data:empty,r.tail.person?motives.at(id):empty,
            p.object_data,p.extras,r.common.slots.size(),masters.at(id)),"saved world runtime bank budget/validation rejected");
        if(p.x!=-16 || p.y!=-16)
            check(result->world->set_position(id,{p.x,p.y,static_cast<std::int16_t>(p.level),static_cast<std::uint8_t>(p.object_data[1])}),
                  "saved world runtime position rejected");
        SavedObjectRelationships rel{id,{}};
        for(const auto& pair:r.common.relationships) rel.relationships.push_back({pair.target_id,pair.values});
        relationships.push_back(std::move(rel));
    }
    for(const auto& item:inbound)
        check(result->world->attach(item.second.first,item.second.second,item.first),"saved world runtime containment rejected");
    check(result->relationships->import_object_relationships(*result->world,relationships),"saved world relationship import rejected");
    result->records=std::move(source);
    return result;
}
} // namespace odh
