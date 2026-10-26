#include "odh/occupancy_projection.hpp"
#include "odh/sha256.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace odh {
namespace {
bool zero(const std::array<std::uint8_t,32>& value) {
    return std::all_of(value.begin(),value.end(),[](auto byte){return byte==0;});
}
std::uint16_t house(const FamilyEntry& family) {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(family.family.house_number)&65535U);
}
void check(bool condition,const char* message) { if(!condition)throw FormatError(message); }
bool dead(const std::set<std::int16_t>& retired,std::int32_t id) {
    // Never truncate a raw 32-bit relationship key into an unrelated actor ID.
    return id>0&&id<=32767&&retired.count(static_cast<std::int16_t>(id));
}
void recount(RelationshipSnapshot& snapshot) {
    snapshot.pair_count=0;snapshot.value_count=0;
    for(const auto& source:snapshot.sources){
        check(source.pairs.size()<=snapshot.limits.pairs-snapshot.pair_count,"occupancy relationship pair budget exceeded");
        snapshot.pair_count+=source.pairs.size();
        for(const auto& pair:source.pairs){
            check(pair.values.size()<=snapshot.limits.values-snapshot.value_count,"occupancy relationship value budget exceeded");
            snapshot.value_count+=pair.values.size();
        }
    }
}
void bound_records(const std::vector<SavedWorldEntity>& records,const SavedWorldLimits& limits) {
    auto words=limits.retained_words,strings=limits.retained_string_bytes,suffix=limits.retained_suffix_bytes;
    const auto consume=[](std::uint64_t& remaining,std::uint64_t amount){
        check(amount<=remaining,"occupancy retirement retained provenance exceeds bounds");remaining-=amount;
    };
    const auto text=[&](const ObjmSavedString& value){
        consume(words,2+(value.padding?1:0));consume(strings,value.bytes.size());
    };
    for(const auto& record:records){
        const auto& common=record.common;const auto& tail=record.tail;
        consume(words,4+3+1+1+8+68+5+16+12+7+2);consume(words,record.saved.prefix.attributes.size());
        for(const auto& frame:common.stack){consume(words,7);consume(words,frame.parameters.size());consume(words,frame.locals.size());}
        for(const auto& pair:common.relationships){consume(words,3);consume(words,pair.values.size());}
        if(common.linked_slot)consume(words,2);
        for(const auto& slot:common.slots){(void)slot;consume(words,2);}
        consume(words,common.dynamic_sprite_flags.size());
        if(tail.multitile)consume(words,7);
        if(tail.portal_count)consume(words,1);
        if(tail.portal_float_bits)consume(words,tail.portal_float_bits->size());
        if(tail.unknown_int_present)consume(words,1);
        consume(suffix,tail.remaining_data.size());
        if(!tail.person)continue;
        const auto& person=*tail.person;consume(words,8+9+16+16+28);consume(words,person.person_data.size());
        for(const auto* value:{&person.body,&person.body_texture,&person.unknown1,&person.unknown2,&person.left_hand,
                &person.left_hand_texture,&person.right_hand,&person.right_hand_texture,&person.head,&person.head_texture,
                &person.animation,&person.carry_animation,&person.base_animation})text(*value);
        for(const auto& value:person.accessories){text(value.name);text(value.binding);}
        for(const auto& value:person.interaction_queue){(void)value;consume(words,14);}
        for(const auto& value:person.object_uses){(void)value;consume(words,3);}
        for(const auto& value:person.motive_deltas){(void)value;consume(words,3);}
    }
}
}

std::optional<InstalledOccupancyRetirement> prepare_installed_occupancy_retirement(
        const NeighborhoodFamilySource& source,const FamilyOverlay& overlay,
        const SessionFamilyLease& destination,std::uint16_t selected_house,
        const std::array<std::uint8_t,32>& house_identity) {
    check(selected_house>=1&&selected_house<=98&&!zero(house_identity)&&!zero(source.identity),
        "occupancy retirement needs exact house and neighborhood provenance");
    const auto replay=replay_family_overlay(source.families,source.identity,overlay);
    const auto target=std::find_if(replay.states.begin(),replay.states.end(),
        [&](const auto& value){return value.family_chunk_id==destination.family_chunk_id;});
    check(target!=replay.states.end()&&target->managed&&target->house_number==selected_house&&
        target->occupancy_epoch==destination.occupancy_epoch&&destination.occupancy_epoch&&
        target->family_identity==destination.family_identity&&source.identity==destination.neighborhood_identity,
        "occupancy retirement destination lease differs from replayed authority");
    const FamilyEntry* original=nullptr;
    for(const auto& family:source.families)if(house(family)==selected_house){
        check(!original,"occupancy retirement source house has multiple families");original=&family;
    }
    if(!original)return std::nullopt;
    const auto final_source=std::find_if(replay.states.begin(),replay.states.end(),
        [&](const auto& value){return value.family_chunk_id==original->chunk_id;});
    check(final_source!=replay.states.end(),"occupancy retirement source family history is unavailable");
    // Full replay forbids simultaneous occupancy, so a live destination at
    // this house implies the earlier source lease ended before its move-in.
    if(final_source->occupancy_epoch==1&&final_source->house_number==selected_house)return std::nullopt;
    check(final_source->occupancy_epoch>=1,"occupancy retirement source epoch is invalid");
    InstalledOccupancyRetirement result;
    result.neighborhood_identity_=source.identity;result.house_identity_=house_identity;
    result.destination_lease_=destination;result.house_number_=selected_house;
    result.source_family_chunk_id_=original->chunk_id;result.expired_guids_=original->family.member_guids;
    std::sort(result.expired_guids_.begin(),result.expired_guids_.end());
    check(std::adjacent_find(result.expired_guids_.begin(),result.expired_guids_.end())==result.expired_guids_.end(),
        "occupancy retirement source roster contains duplicate GUIDs");
    check(std::none_of(result.expired_guids_.begin(),result.expired_guids_.end(),[](auto guid){return guid==0;}),
        "occupancy retirement source roster contains an unknown GUID");
    const auto bytes=encode_family_overlay(overlay);
    result.overlay_identity_=sha256(bytes.data(),bytes.size());
    Sha256 hash;const char domain[]="ODH installed occupancy retirement v1";hash.update(domain,sizeof(domain));
    hash.update(source.identity.data(),source.identity.size());hash.update(house_identity.data(),house_identity.size());
    hash.update(result.overlay_identity_.data(),result.overlay_identity_.size());
    hash.update(destination.family_identity.data(),destination.family_identity.size());
    const auto word=[&](std::uint64_t value,unsigned count){
        std::array<std::uint8_t,8> data{};for(unsigned i=0;i<count;++i)data[i]=static_cast<std::uint8_t>(value>>(8*i));
        hash.update(data.data(),count);
    };
    word(selected_house,2);word(destination.family_chunk_id,2);word(destination.occupancy_epoch,8);
    word(original->chunk_id,2);word(result.expired_guids_.size(),4);
    for(auto guid:result.expired_guids_)word(guid,4);
    result.plan_identity_=hash.finish();return result;
}

OccupancyProjection project_installed_occupancy_retirement(const SavedWorld& source,
        const InstalledOccupancyRetirement& plan,const std::array<std::uint8_t,32>& observed_house,
        const OccupancyDropHandler& drop_held,const SavedWorldLimits& limits) {
    check(observed_house==plan.house_identity(),"occupancy retirement house bytes changed");
    check(source.world&&source.relationships,"occupancy retirement needs a validated saved World");
    check(source.records.size()<=limits.world.entities,"occupancy retirement record budget exceeded");
    bound_records(source.records,limits);
    auto working_limits=limits.world;
    working_limits.reserved_entity_ids=source.world->reserved_entity_ids();
    World working(working_limits,source.world->snapshot().attribute_policy);
    check(working.restore_snapshot(source.world->snapshot()),"occupancy retirement source exceeds World bounds");
    std::map<std::int16_t,const SavedWorldEntity*> records;
    for(const auto& record:source.records){
        check(record.saved.object_id>0&&record.saved.object_id<=32767&&
            records.emplace(static_cast<std::int16_t>(record.saved.object_id),&record).second,
            "occupancy retirement source record identity is invalid");
        const auto* actor=working.find(static_cast<std::int16_t>(record.saved.object_id));
        check(actor&&actor->definition&&actor->definition->guid==record.saved.guid,
            "occupancy retirement source records differ from World");
    }
    check(records.size()==working.snapshot().entities.size(),"occupancy retirement source record coverage is incomplete");
    OccupancyProjection result;std::set<std::int16_t> retired;
    std::vector<Entity> removed_actors;
    std::set<std::uint32_t> seen;
    for(const auto& actor:working.snapshot().entities){
        if(!actor.definition||!std::binary_search(plan.expired_guids().begin(),plan.expired_guids().end(),actor.definition->guid))continue;
        const auto& record=*records.at(actor.id);
        check(actor.kind==EntityKind::avatar&&record.serialized_object_type==2&&record.tail.person&&
            !record.tail.multitile,"occupancy retirement GUID does not identify a plain saved avatar");
        check(seen.insert(actor.definition->guid).second,"occupancy retirement found duplicate source resident GUID");
        result.retired.push_back({actor.definition->guid,actor.id,actor.generation});retired.insert(actor.id);
        removed_actors.push_back(actor);
    }
    for(const auto& group:source.multitile_groups)
        check(std::none_of(group.members.begin(),group.members.end(),[&](auto id){return retired.count(id);}),
            "occupancy retirement cannot split a saved multitile group");
    for(const auto& actor:removed_actors)
        for(auto child:actor.slots)if(child&&!retired.count(child))
            check(bool(drop_held),"occupancy retirement needs a reference-backed held-object placement rule");
    for(auto id:retired)check(working.remove(id),"occupancy retirement could not remove source resident");
    for(const auto& actor:removed_actors){
        std::vector<std::int16_t> held;
        for(auto child:actor.slots)if(child&&!retired.count(child))held.push_back(child);
        if(!held.empty())check(drop_held(working,actor,held),"occupancy retirement held-object placement failed");
    }
    auto normalized=working.snapshot();
    // This is a new logical activation baseline. Preserve every unrelated bank
    // word, but do not leave a dead container in its known saved-bank mirror.
    for(auto& actor:normalized.entities)if(actor.saved_object_banks){
        actor.saved_object_banks->object_data[2]=actor.attachment?actor.attachment->container:0;
        actor.saved_object_banks->object_data[3]=actor.attachment?
            static_cast<std::int16_t>(actor.attachment->slot):actor.unattached_slot;
    }
    check(normalized.entities.size()+retired.size()==records.size(),"occupancy drop rule changed unrelated entity count");
    for(const auto& actor:source.world->snapshot().entities)if(!retired.count(actor.id)){
        const auto* current=working.find(actor.id);
        check(current&&current->generation==actor.generation&&current->definition&&actor.definition&&
            current->definition->guid==actor.definition->guid,"occupancy drop rule deleted or replaced a surviving entity");
    }
    auto relationships=source.relationships->snapshot();
    for(auto it=relationships.sources.begin();it!=relationships.sources.end();){
        if(it->matrix!=RelationshipMatrix::object){++it;continue;}
        if(retired.count(it->id)){it=relationships.sources.erase(it);continue;}
        it->pairs.erase(std::remove_if(it->pairs.begin(),it->pairs.end(),
            [&](const auto& pair){return dead(retired,pair.target);}),it->pairs.end());++it;
    }
    recount(relationships);
    result.candidate=std::make_unique<SavedWorld>();
    auto destination_limits=limits.world;
    destination_limits.reserved_entity_ids.insert(destination_limits.reserved_entity_ids.end(),
        source.world->reserved_entity_ids().begin(),source.world->reserved_entity_ids().end());
    destination_limits.reserved_entity_ids.insert(destination_limits.reserved_entity_ids.end(),retired.begin(),retired.end());
    std::sort(destination_limits.reserved_entity_ids.begin(),destination_limits.reserved_entity_ids.end());
    destination_limits.reserved_entity_ids.erase(std::unique(destination_limits.reserved_entity_ids.begin(),
        destination_limits.reserved_entity_ids.end()),destination_limits.reserved_entity_ids.end());
    result.candidate->world=std::make_unique<World>(destination_limits,normalized.attribute_policy);
    check(result.candidate->world->restore_snapshot(normalized),"occupancy retirement normalized World is invalid");
    result.candidate->relationships=std::make_unique<RelationshipState>(limits.relationships);
    check(result.candidate->relationships->restore_snapshot(relationships),"occupancy retirement relationships exceed bounds");
    result.source_records=source.records;
    result.candidate->multitile_groups=source.multitile_groups;
    result.invalidated_saved_execution=!retired.empty();
    for(const auto& original:source.records){
        const auto id=static_cast<std::int16_t>(original.saved.object_id);
        if(retired.count(id))continue;
        auto record=original;const auto* actor=result.candidate->world->find(id);
        auto& common=record.common;
        check(common.slots.size()==actor->slots.size(),"occupancy retirement slot schema changed");
        for(std::size_t slot=0;slot<common.slots.size();++slot)common.slots[slot].object_id=actor->slots[slot];
        if(common.linked_slot&&dead(retired,common.linked_slot->object_id))common.linked_slot->object_id=0;
        record.saved.prefix.object_data[2]=actor->attachment?actor->attachment->container:0;
        record.saved.prefix.object_data[3]=actor->attachment?static_cast<std::int16_t>(actor->attachment->slot):actor->unattached_slot;
        if(actor->position){record.saved.prefix.x=actor->position->x;record.saved.prefix.y=actor->position->y;
            record.saved.prefix.level=actor->position->level;record.saved.prefix.object_data[1]=actor->position->direction;}
        else {record.saved.prefix.x=-16;record.saved.prefix.y=-16;}
        common.relationships.erase(std::remove_if(common.relationships.begin(),common.relationships.end(),
            [&](const auto& pair){return dead(retired,pair.target_id);}),common.relationships.end());
        common.relationship_count=static_cast<std::int32_t>(common.relationships.size());
        // All source execution remains dormant. Remove only typed stale links;
        // original params/locals/temps/opaque bytes remain available as evidence.
        for(auto& frame:common.stack)if(dead(retired,frame.stack_object_id))frame.stack_object_id=0;
        if(record.tail.person){
            auto& person=*record.tail.person;
            const auto stale=[&](const auto& interaction){return dead(retired,interaction.caller_id)||dead(retired,interaction.target_id);};
            if(stale(person.active_interaction))person.active_interaction={};
            if(stale(person.last_interaction))person.last_interaction={};
            person.interaction_queue.erase(std::remove_if(person.interaction_queue.begin(),person.interaction_queue.end(),stale),person.interaction_queue.end());
            person.interaction_queue_count=static_cast<std::int32_t>(person.interaction_queue.size());
            person.object_uses.erase(std::remove_if(person.object_uses.begin(),person.object_uses.end(),
                [&](const auto& use){return dead(retired,use.target_id);}),person.object_uses.end());
            person.object_use_count=static_cast<std::int32_t>(person.object_uses.size());
        }
        result.candidate->records.push_back(std::move(record));
    }
    result.retired_ids.assign(retired.begin(),retired.end());return result;
}
} // namespace odh
