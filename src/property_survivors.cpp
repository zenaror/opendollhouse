#include "odh/property_survivors.hpp"
#include <algorithm>
#include <map>
#include <set>

namespace odh {
namespace {
void require(bool condition,const char* message) { if(!condition)throw FormatError(message); }
}
PropertySurvivors prepare_property_survivors(const World& source,const RelationshipState& relations,
    const DefinitionIndex& definitions,const std::vector<SavedMultitileGroup>& groups,
    bool complete,const std::vector<std::int16_t>& seeds,
    const std::vector<PropertyDepartingActor>& actors,const SavedWorldLimits& limits) {
    require(complete,"property survivors require a complete current inventory");
    require(groups.size()<=4096 && seeds.size()<=4096 && actors.size()<=4096,
        "property survivor inventory budget exceeded");
    auto source_snapshot=source.snapshot();
    require(source_snapshot.entities.size()<=4096,"property survivor entity budget exceeded");
    auto world_limits=limits.world;
    // A caller's extra exclusions cannot hide live entities or erase prior ones.
    auto reserved=source.reserved_entity_ids();
    reserved.insert(reserved.end(),world_limits.reserved_entity_ids.begin(),world_limits.reserved_entity_ids.end());
    std::sort(reserved.begin(),reserved.end());reserved.erase(std::unique(reserved.begin(),reserved.end()),reserved.end());
    require(reserved.size()<=4096 && (reserved.empty() || reserved.front()>0),"invalid property reservation policy");
    world_limits.reserved_entity_ids=reserved;
    auto working=std::make_unique<World>(world_limits,source_snapshot.attribute_policy);
    require(working->restore_snapshot(source_snapshot),"invalid or oversized current property World");
    std::map<std::int16_t,const Entity*> live;
    std::map<std::int16_t,std::size_t> membership;
    std::set<std::int16_t> actor_ids;
    for(const auto& entity:source_snapshot.entities) {
        require(!entity.ui_ghost,"property survivor preparation rejects UI ghosts");
        live.emplace(entity.id,&entity);
    }
    std::size_t members=0;
    for(std::size_t g=0;g<groups.size();++g) {
        const auto& group=groups[g];
        require(!group.members.empty() && group.members.size()<=4096-members,"invalid property group budget");
        members+=group.members.size();
        require(std::find(group.members.begin(),group.members.end(),group.leader)!=group.members.end(),
            "property group leader is absent");
        const DefinitionCandidate* provenance=nullptr;
        for(const auto id:group.members) {
            require(live.count(id) && membership.emplace(id,g).second,"invalid property group partition");
            const auto& entity=*live.at(id);
            require(entity.definition && entity.definition->guid,"missing current property identity");
            const auto* own=definitions.unique(entity.definition->guid);
            require(own && own->definition.is_multitile==(own->definition.master_id!=0),"invalid or ambiguous property definition");
            require(entity.definition->master_guid==group.master_guid,"property group identity mismatch");
            const auto type=entity.definition_words.find(9);
            require(type==entity.definition_words.end() || static_cast<std::uint16_t>(type->second)==own->definition.object_type,
                "current property type contradicts definition");
            if(own->definition.is_multitile) {
                const auto* master=definitions.unique_master(*own);
                require(master && master->definition.is_multitile && group.master_guid==master->definition.guid,
                    "missing or inconsistent property master");
            } else require(!group.master_guid && group.members.size()==1,"invalid property singleton");
            require(!provenance || (provenance->source==own->source && provenance->asset==own->asset &&
                provenance->definition.master_id==own->definition.master_id),"cross-resource property group");
            provenance=own;
            if(entity.kind==EntityKind::avatar || own->definition.object_type==2)actor_ids.insert(id);
        }
    }
    require(membership.size()==live.size(),"incomplete current property partition");
    std::set<std::int16_t> removed;
    std::vector<std::int16_t> pending;
    for(const auto id:seeds) {
        require(live.count(id) && removed.insert(id).second,"invalid or duplicate property deletion seed");
        pending.push_back(id);
    }
    // Each ID enters the queue once. Adding a child also expands ITS complete
    // group, whose other parts may themselves contain more children.
    for(std::size_t cursor=0;cursor<pending.size();++cursor) {
        const auto id=pending[cursor];
        const auto enqueue=[&](std::int16_t next){if(removed.insert(next).second)pending.push_back(next);};
        for(const auto part:groups[membership.at(id)].members)enqueue(part);
        for(const auto child:live.at(id)->slots)if(child)enqueue(child);
    }
    std::set<std::int16_t> approved;
    for(const auto& actor:actors) {
        require(actor_ids.count(actor.id) && removed.count(actor.id) && approved.insert(actor.id).second,
            "actor approval does not name an exact removed actor");
        const auto& entity=*live.at(actor.id);
        require(entity.generation==actor.generation && entity.definition->guid==actor.guid,
            "stale departing actor identity");
    }
    for(const auto id:actor_ids) {
        require(removed.count(id),"retained property actor needs a fresh activation policy");
        require(approved.count(id),"property deletion reaches an unauthorized actor");
    }
    auto relationships=relations.snapshot();
    // Validate before filtering: deletion must not conceal malformed/stale data.
    RelationshipState relationship_validation(limits.relationships);
    require(relationship_validation.restore_snapshot(relationships),"invalid or oversized current relationships");
    for(const auto& entry:relationships.sources)if(entry.matrix==RelationshipMatrix::object) {
        require(live.count(entry.id) && live.at(entry.id)->generation==entry.generation,
            "stale property object relationship source");
        for(const auto& pair:entry.pairs)
            require(pair.target>0 && pair.target<=32767 && live.count(static_cast<std::int16_t>(pair.target)),
                "noncanonical property object relationship target");
    }
    for(const auto id:removed)require(working->remove(id),"could not remove private property object");
    std::vector<std::int16_t> retired(removed.begin(),removed.end());
    require(working->install_reserved_entity_ids(retired),"property retirement reservation budget exceeded");
    // Reconcile ONLY the known derived containment/self mirrors. Other saved
    // banks remain historical evidence; arbitrary words are never zeroed by ID.
    auto normalized=working->snapshot();
    for(auto& entity:normalized.entities)if(entity.saved_object_banks) {
        entity.saved_object_banks->object_data[2]=entity.attachment?entity.attachment->container:0;
        entity.saved_object_banks->object_data[3]=entity.attachment?
            static_cast<std::int16_t>(entity.attachment->slot):entity.unattached_slot;
        entity.saved_object_banks->object_data[11]=entity.id;
    }
    require(working->publish_snapshot(normalized),"invalid property survivor normalization");
    for(auto it=relationships.sources.begin();it!=relationships.sources.end();) {
        if(it->matrix!=RelationshipMatrix::object){++it;continue;}
        if(removed.count(it->id)){it=relationships.sources.erase(it);continue;}
        it->pairs.erase(std::remove_if(it->pairs.begin(),it->pairs.end(),[&](const auto& pair){
            return removed.count(static_cast<std::int16_t>(pair.target));}),it->pairs.end());
        ++it;
    }
    relationships.pair_count=0;relationships.value_count=0;
    for(const auto& entry:relationships.sources) {
        relationships.pair_count+=entry.pairs.size();
        for(const auto& pair:entry.pairs)relationships.value_count+=pair.values.size();
    }
    auto survivors=std::make_unique<RelationshipState>(limits.relationships);
    require(survivors->restore_snapshot(relationships),"invalid survivor relationships");
    PropertySurvivors result;
    for(const auto& group:groups)if(!removed.count(group.leader))result.groups.push_back(group);
    result.world=std::move(working);result.relationships=std::move(survivors);
    result.removed_ids=std::move(retired);return result;
}
} // namespace odh
