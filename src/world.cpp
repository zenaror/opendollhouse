#include "odh/entity_name.hpp"
#include "odh/world.hpp"
#include "odh/avatar_containment.hpp"
#include "odh/avatar_person_data.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <iterator>
#include <utility>
#include <set>

namespace odh {
namespace {
bool valid_index(std::int16_t index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}
std::size_t entity_words(const Entity& entity){
    return entity.attributes.size()+entity.person_data.size()+entity.slots.size()+entity.object_words.size()+
        entity.motives.size()+entity.definition_words.size()+(entity.entry_points?2*entity.entry_points->size():0)+
        (entity.saved_object_banks?73:0);
}
VmEntityRole scope_role(std::uint8_t scope){return scope==0||scope==3||scope==14||scope==18||scope==20?
    VmEntityRole::caller:VmEntityRole::stack_object;}
bool derived(std::int16_t index) { return index == 2 || index == 3 || index == 11 || index == 67; }
bool valid_position(EntityPosition position) {
    return position.x>=0 && position.y>=0 && position.x<=32767 && position.y<=32767 &&
           position.level>=1 && position.direction<=7;
}
bool valid_appearance_name(const std::string& name) {
    if(name.size()>256)return false;
    for(const unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
        (c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
    return true;
}
bool valid_outfit(const AvatarOutfitReference& outfit){
    if(outfit.kind>AvatarOutfitKind::none||outfit.literal.size()>4096)return false;
    for(const unsigned char c:outfit.literal)if(c<32||c>126)return false;
    for(const auto& hand:outfit.hands){if(hand.size()>1024)return false;
        for(const unsigned char c:hand)if(c<32||c>126)return false;}
    if(outfit.kind!=AvatarOutfitKind::literal&&(!outfit.literal.empty()||
        std::any_of(outfit.hands.begin(),outfit.hands.end(),[](const auto& h){return !h.empty();})))return false;
    return outfit.kind==AvatarOutfitKind::identifier||!outfit.identifier;
}
bool valid_appearance_state(const AvatarAppearanceState& state,const WorldLimits& limits){
    if((state.body&&!valid_outfit(*state.body))||(state.default_daywear&&!valid_outfit(*state.default_daywear)))return false;
    if(state.accessory_assets){
        if(state.accessory_assets->size()>limits.bound_appearance_names||!std::is_sorted(state.accessory_assets->begin(),state.accessory_assets->end())||
            std::adjacent_find(state.accessory_assets->begin(),state.accessory_assets->end())!=state.accessory_assets->end())return false;
        std::size_t bytes=0;for(const auto& asset:*state.accessory_assets){if(!valid_appearance_name(asset))return false;bytes+=asset.size();}
        if(bytes>limits.bound_appearance_name_bytes)return false;
    }
    return true;
}

std::uint8_t direction_notch(float radians) {
    constexpr double pi = 3.141592653589793238462643383279502884;
    const auto notch = static_cast<double>(radians) / (pi / 4.0);
    const auto lower = std::floor(notch);
    const auto fraction = notch - lower;
    // Match Math.Round's midpoint-to-even rule without inheriting the process
    // floating-point rounding mode as nearbyint would.
    const auto rounded = fraction < 0.5 ? lower
                       : fraction > 0.5 ? lower + 1.0
                       : std::fmod(std::abs(lower), 2.0) == 0.0 ? lower : lower + 1.0;
    auto modulo = std::fmod(rounded, 8.0);
    if (modulo < 0.0) modulo += 8.0;
    return static_cast<std::uint8_t>(modulo);
}
float radians_for_notch(std::uint8_t direction) {
    constexpr float pi_over_four = 0.78539816339744830962f;
    return static_cast<float>(direction) * pi_over_four;
}
}
World::World(WorldLimits limits, AttributePolicy attributes)
    : limits_(std::move(limits)), attribute_policy_(attributes) {
    if(!limits_.room_registration_events||limits_.room_registration_events>16384)
        throw std::invalid_argument("World room registration history must contain 1..16384 events");
    room_registration_history_.reserve(limits_.room_registration_events);
    auto& reserved = limits_.reserved_entity_ids;
    if (reserved.size() > 4096 || std::any_of(reserved.begin(), reserved.end(), [](auto id) { return id <= 0; }))
        throw std::invalid_argument("World reserved entity IDs must contain at most 4096 positive IDs");
    std::sort(reserved.begin(), reserved.end());
    if (std::adjacent_find(reserved.begin(), reserved.end()) != reserved.end())
        throw std::invalid_argument("World reserved entity IDs must be unique");
}
bool World::reject_room_reentry() const noexcept {
    if(resolving_current_value_){current_value_reentry_=true;return true;}
    if(!resolving_rooms_&&!resolving_outfit_)return false;
    if(resolving_rooms_)room_resolution_reentered_=true;
    if(resolving_outfit_)outfit_resolution_reentered_=true;
    return true;
}
bool World::set_avatar_outfit_resolver(AvatarOutfitResolver resolver){
    if(reject_room_reentry()||!resolver)return false;
    avatar_outfit_resolver_=std::move(resolver);return true;
}
bool World::evaluate_appearance_lookup(const std::function<void()>& lookup) const {
    if(!lookup)return false;
    if(resolving_rooms_){room_resolution_reentered_=true;return false;}
    // A CurrentOutfit resolver can delegate to the shared Person provider.
    // Keep the outer guard's rejection bit across that nested read-only lookup.
    if(resolving_outfit_){try{lookup();}catch(...){return false;}return !outfit_resolution_reentered_;}
    struct Guard {const World& world;Guard(const World& value):world(value){world.resolving_outfit_=true;world.outfit_resolution_reentered_=false;}
        ~Guard(){world.resolving_outfit_=false;}} guard(*this);
    try{lookup();}catch(...){return false;}
    return !outfit_resolution_reentered_;
}
bool World::set_avatar_outfit(std::int16_t id,const AvatarOutfitReference& outfit,bool update_default,
        std::optional<std::int16_t> current){
    if(reject_room_reentry()||!valid_outfit(outfit))return false;
    const auto found=entities_.find(id);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar||(current&&found->second.person_data.size()<=8))return false;
    auto state=found->second.avatar_appearance.value_or(AvatarAppearanceState{});
    state.body=outfit;if(update_default)state.default_daywear=outfit;
    if(!valid_appearance_state(state,limits_))return false;
    const bool changed=found->second.avatar_appearance!=state||(current&&found->second.person_data[8]!=*current);
    found->second.avatar_appearance=std::move(state);if(current)found->second.person_data[8]=*current;
    if(changed)mark_spatial_change();
    return true;
}
bool World::set_room_resolver(RoomResolver resolver) {
    if(reject_room_reentry()||!resolver||room_capability_revision_==UINT64_MAX)return false;
    room_resolver_=std::move(resolver);++room_capability_revision_;return true;
}
bool World::set_current_value_read_resolver(CurrentValueReadResolver resolver){
    if(reject_room_reentry()||(resolver&&current_value_read_resolver_))return false;
    current_value_read_resolver_=std::move(resolver);return true;
}
bool World::set_current_value_write_observer(CurrentValueWriteObserver observer){
    if(reject_room_reentry()||(observer&&current_value_write_observer_))return false;
    current_value_write_observer_=std::move(observer);return true;
}
bool World::write_current_value(WorldEntityReference identity,std::int16_t before,std::int16_t after){
    struct Lease {bool active=true,called=false,repeated=false;};
    auto lease=std::make_shared<Lease>();resolving_current_value_=true;current_value_reentry_=false;
    const std::function<bool()> store=[this,lease,identity,before,after]{
        if(!lease->active||lease->called||current_value_reentry_){if(lease->called)lease->repeated=true;return false;}
        const auto found=entities_.find(identity.id);
        if(found==entities_.end()||found->second.generation!=identity.generation)return false;
        const auto field=found->second.object_words.find(41);
        if(field==found->second.object_words.end()||field->second!=before)return false;
        field->second=after;lease->called=true;return true;
    };
    bool accepted=false;
    try{accepted=current_value_write_observer_(*this,identity,before,after,store);}catch(...){accepted=false;}
    lease->active=false;resolving_current_value_=false;
    accepted=accepted&&lease->called&&!lease->repeated&&!current_value_reentry_;
    if(!accepted){
        const auto found=entities_.find(identity.id);
        if(found!=entities_.end()&&found->second.generation==identity.generation){
            const auto field=found->second.object_words.find(41);if(field!=found->second.object_words.end())field->second=before;
        }return false;
    }
    if(before!=after)mark_spatial_change();return true;
}
void World::set_definition_word_resolver(DefinitionWordResolver resolver) {
    if(reject_room_reentry())throw std::logic_error("room resolver cannot mutate World capabilities");
    definition_word_resolver_=std::move(resolver);
}
bool World::install_reserved_entity_ids(std::vector<std::int16_t> ids) {
    if(reject_room_reentry())return false;
    if (ids.size() > 4096 || std::any_of(ids.begin(), ids.end(), [](auto id) { return id <= 0; }))
        return false;
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) return false;
    const auto& existing = limits_.reserved_entity_ids;
    auto combined_size = existing.size();
    for (const auto id : ids) {
        if (entities_.count(id)) return false;
        if (!std::binary_search(existing.begin(), existing.end(), id) && ++combined_size > 4096)
            return false;
    }
    if (combined_size == existing.size()) return true;
    std::vector<std::int16_t> combined;
    combined.reserve(combined_size);
    std::set_union(existing.begin(), existing.end(), ids.begin(), ids.end(), std::back_inserter(combined));
    limits_.reserved_entity_ids.swap(combined);
    return true;
}
bool World::entity_id_available(std::int16_t id) const {
    return id > 0 && !std::binary_search(limits_.reserved_entity_ids.begin(), limits_.reserved_entity_ids.end(), id) &&
        !entities_.count(id);
}
WorldSnapshot World::snapshot() const {
    WorldSnapshot result;
    result.attribute_policy=attribute_policy_;
    result.word_count=words_;
    result.generation_sequence=generation_sequence_;
    result.spatial_revision=spatial_revision_;
    result.lockout_count_clock=lockout_count_clock_;
    result.entities.reserve(entities_.size());
    for(const auto& [id,entity]:entities_) result.entities.push_back(entity);
    for(const auto& [reference,entity]:retained_entities_)result.retained_entities.push_back(entity);
    return result;
}
bool World::original_load_active() const noexcept {
    return original_load_ && original_load_->active && *original_load_->active;
}
void World::invalidate_original_load() noexcept {
    if(original_load_ && original_load_->active)*original_load_->active=false;
    original_load_binding_eligible_=false;
}
std::optional<OriginalLoadTraversal> World::bind_original_load_order(
        const std::vector<WorldEntityReference>& order,std::size_t limit,
        std::optional<std::uint32_t> source_version) {
    if(reject_room_reentry() || !original_load_binding_eligible_ || original_load_active() ||
       !limit || limit>32767 || order.size()>limit || order.size()!=entities_.size())return {};
    OriginalLoadState candidate;candidate.limit=limit;
    candidate.people.reserve(limit);
    for(std::size_t i=0;i<order.size();++i) {
        const auto ref=order[i];const auto* entity=find(ref.id);
        if(!is_live_reference(ref) || !entity || entity->dead || entity->ui_ghost ||
           !entity->saved_object_banks || !candidate.nodes.emplace(ref,OriginalLoadNode{
               i?order[i-1]:WorldEntityReference{},i+1<order.size()?order[i+1]:WorldEntityReference{}}).second)return {};
        if(entity->kind==EntityKind::avatar)candidate.people.push_back(ref);
    }
    candidate.active=std::make_shared<bool>(true);
    if(!order.empty()){candidate.first=order.front();candidate.last=order.back();}
    OriginalLoadTraversal lease(candidate.active);
    const bool initial_source_binding=!original_load_.has_value();
    original_load_=std::move(candidate);
    // Version0x37 reads these instance policies from serialized state, which has
    // not been mapped. Other explicit native versions retain constructor zero.
    // Publish only after the complete original cohort and allocations succeed.
    if(initial_source_binding&&source_version&&*source_version!=0x37)
        for(const auto reference:order){
            auto& entity=entities_.at(reference.id);
            entity.suppress_referenced_reset=false;entity.skip_native_recovery_position=false;
        }
    return std::optional<OriginalLoadTraversal>(std::move(lease));
}
OriginalLoadLink World::original_load_head(const OriginalLoadTraversal& lease) const noexcept {
    if(!original_load_active() || lease.active_!=original_load_->active)return {};
    if(!original_load_->first.id)return {OriginalLoadLinkStatus::end,{}};
    if(!is_live_reference(original_load_->first))return {};
    return {OriginalLoadLinkStatus::member,original_load_->first};
}
OriginalLoadLink World::original_load_next(const OriginalLoadTraversal& lease,WorldEntityReference current) const noexcept {
    if(!original_load_active() || lease.active_!=original_load_->active)return {};
    const auto& state=*original_load_;
    const auto live=state.nodes.find(current),retained=state.retained_nodes.find(current);
    const OriginalLoadNode* node=nullptr;
    if(live!=state.nodes.end() && is_live_reference(current))node=&live->second;
    else if(retained!=state.retained_nodes.end() && retained_entities_.count(current))node=&retained->second;
    if(!node)return {};
    const auto next=node->next;
    if(!next.id)return {OriginalLoadLinkStatus::end,{}};
    // Native unlink preserves removed.e0. Its successor can itself be removed
    // later: never rewrite the owned pointer to a different live successor.
    if((state.nodes.count(next) && is_live_reference(next)) ||
       (state.retained_nodes.count(next) && retained_entities_.count(next)))
        return {OriginalLoadLinkStatus::member,next};
    return {};
}
OriginalLoadLink World::original_load_person_at(const OriginalLoadTraversal& lease,std::size_t ordinal) const noexcept {
    if(!original_load_active() || lease.active_!=original_load_->active)return {};
    const auto& people=original_load_->people;
    if(ordinal==people.size())return {OriginalLoadLinkStatus::end,{}};
    if(ordinal>people.size() || !is_live_reference(people[ordinal]))return {};
    return {OriginalLoadLinkStatus::member,people[ordinal]};
}
std::optional<OriginalLoadPersonCursor> World::begin_original_person_notifications(const OriginalLoadTraversal& lease) const noexcept {
    if(!original_load_active()||lease.active_!=original_load_->active)return {};
    OriginalLoadPersonCursor cursor;cursor.world_=this;cursor.active_=original_load_->active;
    cursor.storage_=original_load_->people.data();return cursor;
}
OriginalLoadLink World::original_load_person_at(const OriginalLoadPersonCursor& cursor,std::size_t ordinal) const noexcept {
    if(cursor.world_!=this||!original_load_active())return {};
    const auto active=cursor.active_.lock();
    if(!active||!*active||active!=original_load_->active||cursor.storage_!=original_load_->people.data())return {};
    const auto& people=original_load_->people;
    if(ordinal==people.size())return {OriginalLoadLinkStatus::end,{}};
    if(ordinal>people.size()||!is_live_reference(people[ordinal]))return {};
    return {OriginalLoadLinkStatus::member,people[ordinal]};
}
bool World::stage_original_load_insert(WorldEntityReference ref,OriginalLoadNodes& staged) const {
    if(!original_load_active())return true;
    const auto& state=*original_load_;
    if(state.nodes.size()>=state.limit || state.retained_nodes.size()>=state.limit-state.nodes.size() ||
       state.nodes.count(ref) || state.retained_nodes.count(ref))return false;
    staged.emplace(ref,OriginalLoadNode{});return true;
}
void World::publish_original_load_insert(WorldEntityReference ref,OriginalLoadNodes& staged,bool append) noexcept {
    if(staged.empty() || !original_load_active())return;
    auto& state=*original_load_;
    state.nodes.insert(staged.extract(ref));auto& node=state.nodes.find(ref)->second;
    // The pre-reserved registry cannot reallocate during native notifications.
    const auto* entity=find(ref.id);
    if(entity && entity->kind==EntityKind::avatar)state.people.push_back(ref);
    if(append) {
        node.previous=state.last;
        if(state.last.id)state.nodes.find(state.last)->second.next=ref;else state.first=ref;
        state.last=ref;
    } else {
        node.next=state.first;
        if(state.first.id)state.nodes.find(state.first)->second.previous=ref;else state.last=ref;
        state.first=ref;
    }
}
void World::unlink_original_load(WorldEntityReference ref,bool retain_body) noexcept {
    if(!original_load_active())return;
    auto& state=*original_load_;const auto found=state.nodes.find(ref);
    if(found==state.nodes.end()){invalidate_original_load();return;}
    const auto person=std::find(state.people.begin(),state.people.end(),ref);
    if(person!=state.people.end())state.people.erase(person);
    const auto node=found->second;
    if(node.previous.id)state.nodes.find(node.previous)->second.next=node.next;else state.first=node.next;
    if(node.next.id)state.nodes.find(node.next)->second.previous=node.previous;else state.last=node.previous;
    // Transfer the existing bounded node without allocating. Native unlink
    // splices the live list but does not clear removed.e0 (44387c). Only an
    // actually retained exact body owns that frozen outgoing reference.
    if(retain_body)state.retained_nodes.insert(state.nodes.extract(found));
    else state.nodes.erase(found);
}
bool World::restore_snapshot(const WorldSnapshot& snapshot) {
    if(reject_room_reentry())return false;
    if(!entities_.empty() || !retained_entities_.empty() || words_!=0 || generation_sequence_!=0 || spatial_revision_!=0)
        return false;
    return publish_snapshot(snapshot);
}
bool World::publish_snapshot(const WorldSnapshot& snapshot) {
    return publish_snapshot_impl(snapshot,false);
}
bool World::publish_snapshot_impl(const WorldSnapshot& snapshot,bool runtime_constructor) {
    if(reject_room_reentry())return false;
    if(snapshot.generation_sequence<generation_sequence_ ||
       snapshot.spatial_revision<spatial_revision_||
       (lockout_count_clock_&&!snapshot.lockout_count_clock)) return false;
    if(snapshot.entities.size()>limits_.entities || snapshot.retained_entities.size()>limits_.entities-snapshot.entities.size() ||
       (snapshot.attribute_policy!=AttributePolicy::fixed &&
        snapshot.attribute_policy!=AttributePolicy::ts1_bounded)) return false;
    auto available=limits_.words;
    std::size_t appearance_name_bytes=0;
    // Bound all banks before copying. ObjectData follows its fixed index range,
    // as declare_object_word does, rather than the variable bank_words limit.
    const auto validate_entity=[&](const Entity& entity,bool retained){
        if(entity.name_override&&!valid_entity_name(*entity.name_override))return false;
        if(retained&&(!entity.dead||entity.attachment||(entity.native_slot0&&entity.native_slot0->id)||std::any_of(entity.slots.begin(),entity.slots.end(),[](auto id){return id!=0;})))return false;
        if(entity.lockout_count&&!snapshot.lockout_count_clock)return false;
        if(entity.native_out_of_world_level&&entity.position)return false;
        if(entity.id<=0 || std::binary_search(limits_.reserved_entity_ids.begin(), limits_.reserved_entity_ids.end(), entity.id) ||
           !entity.generation || entity.generation>snapshot.generation_sequence ||
           (entity.kind!=EntityKind::object && entity.kind!=EntityKind::avatar) ||
           entity.attributes.size()>limits_.bank_words || entity.attributes.size()>32768 ||
           entity.person_data.size()>limits_.bank_words || entity.person_data.size()>32768 ||
           entity.slots.size()>limits_.bank_words || entity.slots.size()>32767 ||
           entity.motives.size()>limits_.bank_words || entity.motives.size()>16 ||
           entity.bound_appearances.size()>limits_.bound_appearance_names ||
           (!entity.bound_appearances.empty()&&entity.kind!=EntityKind::avatar) ||
           (!entity.motive_changes.empty() &&
            (entity.kind!=EntityKind::avatar || entity.motives.size()<16 || entity.motive_changes.size()!=16)) ||
           entity.definition_words.size()>limits_.bank_words ||
           (entity.kind!=EntityKind::avatar &&
            (!entity.person_data.empty() || !entity.motives.empty() || entity.kill_timeout)) ||
           (entity.kill_timeout && *entity.kill_timeout < -1) ||
           !std::isfinite(entity.radian_direction) ||
           (entity.position && !valid_position(*entity.position)) ||
           (entity.entry_points && (!entity.definition || entity.entry_points->size()>256 ||
                                    entity.entry_points->size()>limits_.bank_words/2)) ||
           (entity.saved_object_banks &&
            (limits_.bank_words<68 || entity.saved_object_banks->object_data[1]<0 ||
             entity.saved_object_banks->object_data[1]>7))) return false;
        if(entity.avatar_appearance&&(entity.kind!=EntityKind::avatar||!valid_appearance_state(*entity.avatar_appearance,limits_)))return false;
        const auto current=entities_.find(entity.id);
        if(!retained){
            if((current!=entities_.end() && current->second.generation!=entity.generation) ||
               (current==entities_.end() && entity.generation<=generation_sequence_))return false;
        }else if(entity.generation<=generation_sequence_&&!retained_entities_.count({entity.id,entity.generation})&&
            (current==entities_.end()||current->second.generation!=entity.generation))return false;
        for(const auto& [index,value]:entity.object_words)
            if(index<0 || index>=79 || derived(index)) return false;
        for(const auto& [index,value]:entity.definition_words)
            if(index<0 || index>255) return false;
        for(const auto& change:entity.motive_changes)
            if(!std::isfinite(change.fractional))return false;
        if(!std::is_sorted(entity.bound_appearances.begin(),entity.bound_appearances.end())||
           std::adjacent_find(entity.bound_appearances.begin(),entity.bound_appearances.end())!=entity.bound_appearances.end())return false;
        for(const auto& name:entity.bound_appearances){
            if(!valid_appearance_name(name)||name.size()>limits_.bound_appearance_name_bytes-appearance_name_bytes)return false;
            appearance_name_bytes+=name.size();
        }
        if(entity.saved_object_banks) {
            if(!entity.definition) return false;
            for(std::int16_t index=0;index<68;++index)
                if(!derived(index) && !entity.object_words.count(index)) return false;
        } else if(entity.unattached_slot!=-1) return false;
        for(const auto count:{entity.attributes.size(),entity.person_data.size(),entity.slots.size(),
                              entity.motives.size(),entity.object_words.size(),entity.definition_words.size(),
                              entity.entry_points?2*entity.entry_points->size():std::size_t{0},
                              entity.saved_object_banks?std::size_t{73}:std::size_t{0}}) {
            if(count>available) return false;
            available-=count;
        }
        return true;
    };
    for(const auto& entity:snapshot.entities)if(!validate_entity(entity,false))return false;
    for(const auto& entity:snapshot.retained_entities)if(!validate_entity(entity,true))return false;
    if(snapshot.word_count!=limits_.words-available) return false;
    std::map<WorldEntityReference,Entity> retained_candidate;
    std::map<std::int16_t,Entity> candidate;
    std::map<std::uint64_t,std::int16_t> generations;
    for(const auto& entity:snapshot.entities) {
        if(!candidate.emplace(entity.id,entity).second ||
           !generations.emplace(entity.generation,entity.id).second) return false;
    }
    WorldEntityReference previous{};
    for(const auto& entity:snapshot.retained_entities){const WorldEntityReference reference{entity.id,entity.generation};
        if(!(previous<reference)||!retained_candidate.emplace(reference,entity).second||
           !generations.emplace(entity.generation,entity.id).second)return false;
        previous=reference;
    }
    for(const auto& [id,entity]:candidate) {
        for(std::size_t slot=0;slot<entity.slots.size();++slot) {
            const auto occupant=entity.slots[slot];
            if(!occupant) continue;
            const auto child=candidate.find(occupant);
            if(child==candidate.end() || !child->second.attachment ||
               child->second.attachment->container!=id || child->second.attachment->slot!=slot) return false;
        }
        if(entity.attachment) {
            const auto& attachment=*entity.attachment;
            const auto parent=candidate.find(attachment.container);
            if(parent==candidate.end() || attachment.slot>=parent->second.slots.size() ||
               parent->second.slots[attachment.slot]!=id) return false;
            if(attachment.positioned && (bool(entity.position)!=bool(parent->second.position) ||
               (entity.position&&(entity.position->x!=parent->second.position->x ||
               entity.position->y!=parent->second.position->y ||
               entity.position->level!=parent->second.position->level)))) return false;
            if(attachment.positioned&&!entity.position&&entity.kind==EntityKind::object){
                const auto direction=entity.object_words.find(1);
                if(direction==entity.object_words.end()||direction->second<0||direction->second>7)return false;
            }
        }
        if(entity.native_slot0){
            auto link=*entity.native_slot0;std::size_t depth=0;
            while(link.id){
                if(++depth>candidate.size()||link.id==id)return false;
                const auto child=candidate.find(link.id);
                if(child==candidate.end()||child->second.generation!=link.generation)return false;
                const auto rawslot=child->second.object_words.find(14),rawparent=child->second.object_words.find(26);
                if(child->second.attachment||rawslot==child->second.object_words.end()||rawslot->second!=0||
                   rawparent==child->second.object_words.end())return false;
                // Direct reciprocity is checked independently at every owner.
                if(depth==1&&rawparent->second!=id)return false;
                if(!child->second.native_slot0)break;
                link=*child->second.native_slot0;
            }
            if(!entity.native_slot0->id&&entity.native_slot0->generation)return false;
        }
        // Every ancestry chain must terminate, including unpositioned links.
        auto ancestor=&entity;
        std::size_t depth=0;
        while(ancestor->attachment) {
            if(++depth>candidate.size()) return false;
            const auto parent=candidate.find(ancestor->attachment->container);
            if(parent==candidate.end()) return false;
            ancestor=&parent->second;
        }
        // Do not reinterpret legacy unknown raw26. Follow zero ancestry only
        // when the distinct stored backing owner authenticates the edge.
        ancestor=&entity;depth=0;
        for(;;){
            if(++depth>candidate.size())return false;
            if(ancestor->attachment){ancestor=&candidate.at(ancestor->attachment->container);continue;}
            const auto rawslot=ancestor->object_words.find(14),rawparent=ancestor->object_words.find(26);
            if(rawslot==ancestor->object_words.end()||rawslot->second!=0||rawparent==ancestor->object_words.end())break;
            const auto owner=candidate.find(rawparent->second);
            if(owner==candidate.end()||!owner->second.native_slot0||
               *owner->second.native_slot0!=WorldEntityReference{ancestor->id,ancestor->generation})break;
            ancestor=&owner->second;
        }
    }
    bool visibility_changed=false;
    for(const auto& [id,current]:entities_){
        const auto next=candidate.find(id);
        if(next!=candidate.end()&&next->second.ui_ghost!=current.ui_ghost){visibility_changed=true;break;}
    }
    if(visibility_changed&&entity_visibility_revision_==std::numeric_limits<std::uint64_t>::max())return false;
    if(!runtime_constructor)invalidate_original_load();
    entities_.swap(candidate);
    retained_entities_.swap(retained_candidate);
    if(visibility_changed)++entity_visibility_revision_;
    words_=snapshot.word_count;
    generation_sequence_=snapshot.generation_sequence;
    spatial_revision_=snapshot.spatial_revision;
    lockout_count_clock_=snapshot.lockout_count_clock;
    attribute_policy_=snapshot.attribute_policy;
    if(slot_publication_epoch_!=std::numeric_limits<std::uint64_t>::max())++slot_publication_epoch_;
    mark_room_registration(0,0,{},{},true);
    return true;
}
std::optional<std::uint64_t> World::room_registration_revision() const noexcept {
    return room_registration_serial_==UINT64_MAX?std::nullopt:
        std::optional<std::uint64_t>{room_registration_serial_};
}
bool World::room_registration_events_since(std::uint64_t serial,
    std::vector<WorldRoomRegistrationEvent>& out) const {
    out.clear();
    if(!room_registration_revision()||serial>room_registration_serial_)return false;
    if(serial==room_registration_serial_)return true;
    const auto count=room_registration_history_.size();
    if(!count)return false;
    const auto first=count==limits_.room_registration_events?room_registration_cursor_:0;
    if(room_registration_history_[first].serial>serial+1)return false;
    std::vector<WorldRoomRegistrationEvent> copy;copy.reserve(count);
    for(std::size_t n=0;n<count;++n){const auto& event=room_registration_history_[(first+n)%count];
        if(event.serial>serial)copy.push_back(event);}
    out=std::move(copy);return true;
}
void World::mark_room_registration(std::int16_t id,std::uint64_t generation,
    std::optional<EntityPosition> before,std::optional<EntityPosition> after,bool all_rooms) noexcept {
    if(room_registration_serial_==UINT64_MAX)return;
    if(!all_rooms){const auto* entity=find(id);if(entity&&entity->ui_ghost)return;}
    const WorldRoomRegistrationEvent event{++room_registration_serial_,generation,id,before,after,all_rooms};
    if(room_registration_history_.size()<limits_.room_registration_events)room_registration_history_.push_back(event);
    else {room_registration_history_[room_registration_cursor_]=event;
        room_registration_cursor_=(room_registration_cursor_+1)%limits_.room_registration_events;}
}
void World::mark_spatial_change() noexcept {
    if(spatial_revision_!=std::numeric_limits<std::uint64_t>::max())++spatial_revision_;
    notify_spatial_change();
}
void World::notify_spatial_change() noexcept {
    if(spatial_change_observer_&&!notifying_spatial_change_){
        notifying_spatial_change_=true;
        try{spatial_change_observer_(*this,spatial_revision_);}catch(...){/* Invalidation never vetoes a completed source publication. */}
        notifying_spatial_change_=false;
    }
}
bool World::set_spatial_change_observer(SpatialChangeObserver observer){
    if(reject_room_reentry()||notifying_spatial_change_)return false;
    spatial_change_observer_=std::move(observer);return true;
}
bool World::create(std::int16_t id, EntityKind kind, EntityLayout layout) {
    if(reject_room_reentry())return false;
    if (!entity_id_available(id) || generation_sequence_==std::numeric_limits<std::uint64_t>::max() ||
        (kind != EntityKind::object && kind != EntityKind::avatar) ||
        entities_.count(id) || entities_.size()+retained_entities_.size() >= limits_.entities ||
        layout.attributes > limits_.bank_words || layout.person_data > limits_.bank_words ||
        layout.slots > limits_.bank_words || layout.motives > limits_.bank_words ||
        layout.motives > 16 || layout.attributes > 32768 ||
        layout.person_data > 32768 || layout.slots > 32767 ||
        (kind != EntityKind::avatar && (layout.person_data != 0 || layout.motives != 0))) return false;
    // Subtract before summing, so even caller-provided limits cannot overflow.
    auto available = limits_.words - words_;
    for (const auto count : {layout.attributes, layout.person_data, layout.slots, layout.motives}) {
        if (count > available) return false;
        available -= count;
    }
    const auto count = layout.attributes + layout.person_data + layout.slots + layout.motives;
    const WorldEntityReference reference{id,generation_sequence_+1};OriginalLoadNodes staged_links;
    if(!stage_original_load_insert(reference,staged_links))return false;
    entities_.emplace(id, Entity{id,kind,std::vector<std::int16_t>(layout.attributes),
        std::vector<std::int16_t>(layout.person_data),std::vector<std::int16_t>(layout.slots),{},
        std::nullopt,std::nullopt,reference.generation,{},std::vector<std::int16_t>(layout.motives),
        {},{},{},std::int16_t{-1},true,false,std::nullopt,0.0f,{}, {},0,{},{}});
    if (kind == EntityKind::avatar) entities_.at(id).kill_timeout = -1;
    generation_sequence_=reference.generation;
    publish_original_load_insert(reference,staged_links,false);
    words_ += count;
    mark_spatial_change();
    return true;
}
bool World::create_runtime_object(std::int16_t id, std::uint32_t guid,
    EntityLayout layout, const std::vector<EntryPoint>& entry_points,
    std::optional<std::uint32_t> master_guid) {
    if(reject_room_reentry())return false;
    constexpr std::size_t runtime_object_words = 75; // 0..78 except derived 2,3,11,67.
    if (!entity_id_available(id) || guid == 0 || (master_guid && (!*master_guid || *master_guid == guid)) ||
        generation_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
        layout.person_data != 0 || layout.motives != 0 ||
        layout.attributes > limits_.bank_words || layout.attributes > 32768 ||
        layout.slots > limits_.bank_words || layout.slots > 32767 ||
        runtime_object_words > limits_.bank_words || entry_points.size() > 256 ||
        entry_points.size() > limits_.bank_words / 2) return false;

    auto available = limits_.words - words_;
    for (const auto count : {layout.attributes, layout.slots, runtime_object_words,
                             2 * entry_points.size()}) {
        if (count > available) return false;
        available -= count;
    }

    const WorldEntityReference reference{id,generation_sequence_+1};OriginalLoadNodes staged_links;
    if(!stage_original_load_insert(reference,staged_links))return false;
    auto candidate = snapshot();
    Entity entity{};
    entity.id = id;
    entity.kind = EntityKind::object;
    entity.attributes.resize(layout.attributes, 0);
    entity.slots.resize(layout.slots, 0);
    entity.generation = generation_sequence_ + 1;
    entity.definition = EntityType{guid, master_guid};
    entity.entry_points = entry_points;
    // Native431b5e constructs record0 via46dfaf (occupant+10=0), then
    // appends authored SLOT records. This is actual factory state, not an
    // assumption about generic World::create or an older saved snapshot.
    entity.native_slot0=WorldEntityReference{};
    // Native431b5e calls433ef5((-16,-16),1,0) during real construction.
    entity.native_out_of_world_level=1;
    for (std::int16_t word = 0; word <= 78; ++word) {
        if (!derived(word)) entity.object_words.emplace(word, 0);
    }
    entity.object_words.at(14)=-1; //431b5e native hierarchy starts unparented.
    candidate.entities.push_back(std::move(entity));
    candidate.word_count = words_ + (limits_.words - words_ - available);
    candidate.generation_sequence = generation_sequence_ + 1;
    if (candidate.spatial_revision != std::numeric_limits<std::uint64_t>::max())
        ++candidate.spatial_revision;
    if(!publish_snapshot_impl(candidate,true))return false;
    publish_original_load_insert(reference,staged_links,false);
    // AddEntity is visible while still out of world, before Init or placement.
    // publish_snapshot already advanced the revision and publication epoch once.
    notify_spatial_change();
    return true;
}
bool World::create_saved_entity(std::int16_t id, EntityKind kind, std::uint32_t guid,
    const std::vector<std::int16_t>& attributes,
    const std::vector<std::int16_t>& person_data,
    const std::vector<std::int16_t>& motives,
    const std::array<std::int16_t,68>& object_data,
    const std::array<std::int16_t,5>& extras, std::size_t slots,
    std::optional<std::uint32_t> master_guid) {
    if(reject_room_reentry())return false;
    if (!entity_id_available(id) || !guid || (master_guid && !*master_guid) ||
        generation_sequence_==std::numeric_limits<std::uint64_t>::max() ||
        (kind!=EntityKind::object && kind!=EntityKind::avatar) ||
        entities_.count(id) || entities_.size()+retained_entities_.size()>=limits_.entities ||
        attributes.size()>limits_.bank_words || person_data.size()>limits_.bank_words ||
        motives.size()>limits_.bank_words || slots>limits_.bank_words ||
        object_data.size()>limits_.bank_words || extras.size()>limits_.bank_words ||
        attributes.size()>32768 || person_data.size()>32768 || slots>32767 ||
        motives.size()>16 || object_data[1]<0 || object_data[1]>7 ||
        (kind!=EntityKind::avatar && (!person_data.empty() || !motives.empty()))) return false;
    auto available=limits_.words-words_;
    for (const auto count : {attributes.size(),person_data.size(),motives.size(),slots,
                             object_data.size(),extras.size(),std::size_t{64}}) {
        if (count>available) return false;
        available-=count;
    }
    const auto count=limits_.words-words_-available;
    const auto generation=generation_sequence_+1;
    const WorldEntityReference reference{id,generation};OriginalLoadNodes staged_links;
    if(!stage_original_load_insert(reference,staged_links))return false;
    Entity candidate{id,kind,attributes,person_data,std::vector<std::int16_t>(slots),{},
        {},{},generation,EntityType{guid,master_guid},motives,{},{},
        SavedObjectBanks{object_data,extras},object_data[2]==0 ? object_data[3] : std::int16_t{-1},
        true,false,std::nullopt,0.0f,{}, {},0,{},{}};
    for (std::int16_t index=0;index<68;++index)
        if (!derived(index)) candidate.object_words.emplace(index,object_data[static_cast<std::size_t>(index)]);
    entities_.emplace(id,std::move(candidate));
    publish_original_load_insert(reference,staged_links,true);
    if (kind == EntityKind::avatar) entities_.at(id).kill_timeout = -1;
    generation_sequence_=generation;
    words_+=count;
    mark_spatial_change();
    return true;
}
const Entity* World::resolve_reference(WorldEntityReference reference) const {
    if(reference.id<=0||!reference.generation)return nullptr;
    const auto* live=find(reference.id);if(live&&live->generation==reference.generation)return live;
    const auto dead=retained_entities_.find(reference);return dead==retained_entities_.end()?nullptr:&dead->second;
}
std::optional<bool> World::suppress_referenced_reset(WorldEntityReference reference) const {
    const auto* body=resolve_reference(reference);
    return body?body->suppress_referenced_reset:std::nullopt;
}
bool World::set_suppress_referenced_reset(WorldEntityReference reference,bool value){
    if(reject_room_reentry())return false;
    const auto live=entities_.find(reference.id);
    if(live!=entities_.end()&&reference.generation&&live->second.generation==reference.generation){
        live->second.suppress_referenced_reset=value;return true;
    }
    const auto retained=retained_entities_.find(reference);
    if(retained==retained_entities_.end())return false;
    retained->second.suppress_referenced_reset=value;return true;
}
std::optional<bool> World::skip_native_recovery_position(WorldEntityReference reference) const {
    const auto* body=resolve_reference(reference);
    return body?body->skip_native_recovery_position:std::nullopt;
}
bool World::set_skip_native_recovery_position(WorldEntityReference reference,bool value){
    if(reject_room_reentry())return false;
    const auto live=entities_.find(reference.id);
    if(live!=entities_.end()&&reference.generation&&live->second.generation==reference.generation){
        live->second.skip_native_recovery_position=value;return true;
    }
    const auto retained=retained_entities_.find(reference);
    if(retained==retained_entities_.end())return false;
    retained->second.skip_native_recovery_position=value;return true;
}
bool World::native_hierarchy_would_cycle(WorldEntityReference parent,WorldEntityReference child) const {
    auto ancestor=parent;std::size_t depth=0;
    while(ancestor.id){
        if(ancestor==child||++depth>entities_.size())return true;
        const auto* current=find(ancestor.id);
        if(!current||current->generation!=ancestor.generation)return true;
        if(current->attachment){
            const auto owner=attachment_parent(ancestor);if(!owner||!is_live_reference(*owner))return true;
            ancestor=*owner;continue;
        }
        const auto rawslot=current->object_words.find(14),rawparent=current->object_words.find(26);
        if(rawslot==current->object_words.end()||rawslot->second!=0||rawparent==current->object_words.end())break;
        const auto* owner=find(rawparent->second);
        if(!owner||!owner->native_slot0||*owner->native_slot0!=ancestor)break;
        ancestor={owner->id,owner->generation};
    }
    return false;
}
bool World::native_slot0_detached(WorldEntityReference reference) const {
    const auto* body=resolve_reference(reference);
    if(!body||(body->native_slot0&&body->native_slot0->id))return false;
    for(const auto& item:entities_)if(item.second.native_slot0&&*item.second.native_slot0==reference)return false;
    return true;
}
std::optional<WorldEntityReference> World::native_slot0(WorldEntityReference reference) const {
    const auto* body=resolve_reference(reference);
    return body?body->native_slot0:std::nullopt;
}
bool World::publish_native_slot0(WorldEntityReference parent,WorldEntityReference child){
    if(reject_room_reentry()||!is_live_reference(parent))return false;
    if(child.id==0){
        if(child.generation)return false;
    }else{
        if(parent==child||!is_live_reference(child))return false;
        const auto* body=find(child.id);
        const auto slot=body->object_words.find(14),owner=body->object_words.find(26);
        if(body->attachment||slot==body->object_words.end()||slot->second!=0||
           owner==body->object_words.end()||owner->second!=parent.id)return false;
        for(const auto& item:entities_)if(item.first!=parent.id&&item.second.native_slot0&&*item.second.native_slot0==child)return false;
        if(native_hierarchy_would_cycle(parent,child))return false;
        auto cursor=child;std::size_t depth=0;
        while(cursor.id){
            if(cursor==parent||++depth>entities_.size())return false;
            const auto* next=find(cursor.id);
            if(!next||next->generation!=cursor.generation)return false;
            if(!next->native_slot0)break;
            cursor=*next->native_slot0;
        }
    }
    entities_.at(parent.id).native_slot0=child;return true;
}
bool World::is_live_reference(WorldEntityReference reference) const {
    const auto* entity=find(reference.id);return entity&&reference.generation&&entity->generation==reference.generation;
}
std::optional<EntityPosition> World::native_position(WorldEntityReference reference) const {
    const auto* body=resolve_reference(reference);if(!body)return {};
    if(body->position)return body->position;
    if(!body->native_out_of_world_level)return {};
    const auto heading=body->object_words.find(1);
    if(heading==body->object_words.end()||heading->second<0||heading->second>7)return {};
    return EntityPosition{-16,-16,*body->native_out_of_world_level,static_cast<std::uint8_t>(heading->second)};
}
bool World::set_native_out_of_world_level(WorldEntityReference reference,std::int16_t level){
    if(reject_room_reentry()||!is_live_reference(reference))return false;
    auto& body=entities_.at(reference.id);if(body.position)return false;
    body.native_out_of_world_level=level;return true;
}
bool World::publish_lookup_selector(WorldEntityReference reference,std::uint16_t selector,bool enabled){
    if(reject_room_reentry()||!selector)return false;
    Entity* body=nullptr;
    const auto live=entities_.find(reference.id);
    if(live!=entities_.end()&&reference.generation&&live->second.generation==reference.generation)body=&live->second;
    else {const auto retained=retained_entities_.find(reference);if(retained!=retained_entities_.end())body=&retained->second;}
    if(!body)return false;
    //44582f clears only selector<<16, then conditionally sets those same bits.
    body->lookup_selectors=static_cast<std::uint16_t>((body->lookup_selectors&~selector)|(enabled?selector:0));
    body->known_lookup_selectors|=selector;
    return true;
}
std::optional<std::uint16_t> World::lookup_selectors(WorldEntityReference reference,std::uint16_t required) const{
    const auto* body=resolve_reference(reference);
    if(!body||(body->known_lookup_selectors&required)!=required)return {};
    return static_cast<std::uint16_t>(body->lookup_selectors&required);
}
bool World::reference_is_dead(WorldEntityReference reference) const {
    const auto* entity=find(reference.id);
    return (entity&&entity->generation==reference.generation&&entity->dead)||retained_entities_.count(reference)!=0;
}
bool World::mark_dead(WorldEntityReference reference){
    if(reject_room_reentry())return false;
    if(retained_entities_.count(reference))return true;
    const auto found=entities_.find(reference.id);if(found==entities_.end()||!reference.generation||found->second.generation!=reference.generation)return false;
    found->second.dead=true;return true;
}
const Entity* World::frame_entity(const VmFrame& frame,VmEntityRole role,bool live_only) const {
    if(!vm_context_references_valid(frame.context))return nullptr;
    const auto reference=vm_context_reference(frame.context,role);
    if(!frame.context.entity_references_bound)return find(reference.id);
    return live_only?(is_live_reference(reference)?find(reference.id):nullptr):resolve_reference(reference);
}
Entity* World::frame_entity_mutable(const VmFrame& frame,VmEntityRole role){
    return const_cast<Entity*>(static_cast<const World&>(*this).frame_entity(frame,role));
}
bool World::bind_context_live(VmContext& context) const {
    if(!vm_context_references_valid(context))return false;
    if(context.entity_references_bound)return true;
    auto bound=context;bound.entity_references_bound=true;
    for(const auto role:{VmEntityRole::caller,VmEntityRole::callee,VmEntityRole::stack_object}){
        auto reference=vm_context_reference(context,role);const auto* entity=find(reference.id);
        reference.generation=entity?entity->generation:0;
        if(!vm_context_set_reference(bound,role,reference))return false;
    }
    context=std::move(bound);return true;
}
bool World::remove_retaining_body(WorldEntityReference reference){
    if(reject_room_reentry()||!is_live_reference(reference)||retained_entities_.count(reference)||!native_slot0_detached(reference))return false;
    const auto* initial=find(reference.id);std::vector<std::int16_t> detach_ids;
    if(initial->attachment)detach_ids.push_back(initial->id);
    for(std::size_t slot=0;slot<initial->slots.size();++slot)if(initial->slots[slot]){
        const auto* child=find(initial->slots[slot]);
        if(!child||!child->attachment||child->attachment->container!=reference.id||child->attachment->slot!=slot)return false;
        detach_ids.push_back(child->id);
    }
    PhysicalDetachNodes native_nodes;if(!stage_physical_detaches(detach_ids,native_nodes))return false;
    auto staged=retained_entities_.emplace(reference,Entity{});
    struct RemoveEmpty{decltype(retained_entities_)& bodies;WorldEntityReference key;bool published=false;
        ~RemoveEmpty(){if(!published)bodies.erase(key);}} cleanup{retained_entities_,reference};
    const auto found=entities_.find(reference.id);auto& entity=found->second;
    if(entity.attachment){const auto parent=attachment_parent(reference);if(!parent||!is_live_reference(*parent))return false;}
    publish_physical_detach_nodes(native_nodes);
    if(entity.attachment&&!detach(entity.attachment->container,entity.attachment->slot))return false;
    for(std::size_t slot=0;slot<entity.slots.size();++slot)if(!detach(entity.id,slot))return false;
    unlink_original_load(reference,true);
    entity.dead=true;staged.first->second=std::move(entity);entities_.erase(found);cleanup.published=true;
    mark_spatial_change();return true;
}
bool World::retired_container_active(const RetiredContainer& token) const {
    return token.owner_==this&&token.active_&&*token.active_&&
        retained_entities_.count(token.parent_)!=0;
}
std::optional<WorldEntityReference> World::attachment_parent(WorldEntityReference child) const {
    const auto* entity=resolve_reference(child);
    if(!entity||!entity->attachment)return {};
    const auto attachment=*entity->attachment;
    std::optional<WorldEntityReference> result;
    const auto match=[&](const Entity& parent){
        if(parent.id!=attachment.container||attachment.slot>=parent.slots.size()||
           parent.slots[attachment.slot]!=child.id)return true;
        if(result)return false;
        result=WorldEntityReference{parent.id,parent.generation};return true;
    };
    const auto current=entities_.find(attachment.container);
    if(current!=entities_.end()&&!match(current->second))return {};
    for(const auto& entry:retained_entities_)if(!match(entry.second))return {};
    return result;
}
bool World::remove_retaining_contents(WorldEntityReference reference,
        const std::function<bool(const RetiredContainer&)>& operation){
    if(reject_room_reentry()||!operation||!is_live_reference(reference)||
       retained_entities_.count(reference)||!native_slot0_detached(reference))return false;
    // Allocate all lease/body storage before publishing the removal.
    auto active=std::make_shared<bool>(true);
    auto released=std::make_shared<std::set<WorldEntityReference>>();
    auto pin=retained_container_leases_.emplace(reference,active);
    if(!pin.second)return false;
    struct Unpin {decltype(retained_container_leases_)& leases;WorldEntityReference key;
        ~Unpin(){leases.erase(key);}} unpin{retained_container_leases_,reference};
    auto staged=retained_entities_.emplace(reference,Entity{});
    struct Cleanup {decltype(retained_entities_)& bodies;WorldEntityReference key;
        std::shared_ptr<bool> active;bool published=false;
        ~Cleanup(){*active=false;if(!published)bodies.erase(key);}} cleanup{retained_entities_,reference,active};
    const auto found=entities_.find(reference.id);auto& entity=found->second;
    // Validate the complete indexed reciprocal child tree before its parent
    // leaves the numeric index. Each child's pose is read later, in slot order.
    for(std::size_t slot=0;slot<entity.slots.size();++slot)if(entity.slots[slot]){
        const auto* child=find(entity.slots[slot]);
        if(!child||!child->attachment||child->attachment->container!=reference.id||
           child->attachment->slot!=slot)return false;
    }
    if(entity.attachment){const auto parent=attachment_parent(reference);
        if(!parent||!is_live_reference(*parent)||!detach(parent->id,entity.attachment->slot))return false;}
    unlink_original_load(reference,true);
    entity.dead=true;staged.first->second=std::move(entity);entities_.erase(found);
    cleanup.published=true;
    RetiredContainer token;token.owner_=this;token.parent_=reference;token.active_=active;token.released_=released;
    mark_spatial_change();
    if(!operation(token)||!retired_container_active(token))return false;
    const auto& body=retained_entities_.at(reference);
    return std::none_of(body.slots.begin(),body.slots.end(),[](auto id){return id!=0;});
}
bool World::retired_child_authorized(const RetiredContainer& token,WorldEntityReference child) const {
    if(!retired_container_active(token)||!is_live_reference(child))return false;
    if(token.released_&&token.released_->count(child))return true;
    const auto parent=attachment_parent(child);return parent&&*parent==token.parent_;
}
std::optional<std::size_t> World::retired_slot_count(const RetiredContainer& token) const {
    if(!retired_container_active(token))return {};
    return logical_containment_slots(retained_entities_.at(token.parent_));
}
std::optional<WorldEntityReference> World::retired_slot(const RetiredContainer& token,std::size_t slot) const {
    const auto count=retired_slot_count(token);if(!count||slot>=*count)return {};
    const auto& body=retained_entities_.at(token.parent_);
    if(slot>=body.slots.size()||!body.slots[slot])return WorldEntityReference{};
    const auto* child=find(body.slots[slot]);
    if(!child||!child->attachment||child->attachment->container!=body.id||child->attachment->slot!=slot)return {};
    const auto parent=attachment_parent({child->id,child->generation});
    if(!parent||*parent!=token.parent_)return {};
    return WorldEntityReference{child->id,child->generation};
}
bool World::copy_retired_position(const RetiredContainer& token,WorldEntityReference child){
    if(reject_room_reentry()||!retired_container_active(token)||!is_live_reference(child))return false;
    const auto parent=attachment_parent(child);if(!parent||*parent!=token.parent_)return false;
    // VMEntity.Position is the child's own stored value, not a visual or
    // ancestor-derived position. This dead-body assignment has no index event.
    auto& body=retained_entities_.at(token.parent_);
    auto position=find(child.id)->position;
    if(position){
        const auto heading=body.object_words.find(1);
        if(body.position)position->direction=body.position->direction;
        else if(heading!=body.object_words.end()&&heading->second>=0&&heading->second<=7)
            position->direction=static_cast<std::uint8_t>(heading->second);
        else return false;
    }
    body.native_out_of_world_level=position?std::nullopt:find(child.id)->native_out_of_world_level;
    body.position=position;
    return true;
}
bool World::detach_retained_slot(const RetiredContainer& token,std::size_t slot,WorldEntityReference child){
    if(reject_room_reentry())return false;
    const auto actual=retired_slot(token,slot);
    if(!actual||*actual!=child||!child.id)return false;
    if(!token.released_)return false;
    std::map<std::int16_t,std::int16_t> native_nodes;
    if(!stage_native_physical_parent(entities_.at(child.id),native_nodes))return false;
    token.released_->insert(child); // Allocate before the physical detach.
    auto& entity=entities_.at(child.id);entity.attachment.reset();
    publish_native_physical_parent(entity,native_nodes,-1,0);
    if(entity.saved_object_banks)entity.unattached_slot=-1;
    retained_entities_.at(token.parent_).slots[slot]=0;
    mark_spatial_change();return true;
}
bool World::collect_retained_bodies(const std::vector<WorldEntityReference>& roots,std::vector<WorldEntityReference>* collected){
    if(reject_room_reentry()||roots.size()>limits_.entities*16)return false;
    std::set<WorldEntityReference> retained;
    for(const auto reference:roots){if(!reference.generation)continue;
        if(!resolve_reference(reference))return false;
        retained.insert(reference);}
    // Source Delete's local parent pointer remains a strong reference through
    // the final child placement, including after its last occupied slot clears.
    for(const auto& entry:retained_container_leases_){const auto lease=entry.second.lock();
        if(lease&&*lease&&retained_entities_.count(entry.first))retained.insert(entry.first);}
    std::vector<WorldEntityReference> removed;
    if(collected){removed.reserve(retained_entities_.size());for(const auto& entry:retained_entities_)
        if(!retained.count(entry.first))removed.push_back(entry.first);}
    for(const auto& entry:retained_entities_)if(std::any_of(entry.second.slots.begin(),entry.second.slots.end(),[](auto id){return id!=0;}))return false;
    for(auto it=retained_entities_.begin();it!=retained_entities_.end();){
        if(retained.count(it->first)){++it;continue;}
        if(original_load_)original_load_->retained_nodes.erase(it->first);
        words_-=entity_words(it->second);it=retained_entities_.erase(it);
    }
    if(collected)collected->swap(removed);
    return true;
}
const Entity* World::find(std::int16_t id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : &found->second;
}
std::optional<std::int16_t> World::next_entity_of_guid(std::int16_t current,std::uint32_t guid) const {
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it)
        if(!it->second.ui_ghost&&it->second.definition&&it->second.definition->guid==guid)return it->first;
    return {};
}
std::optional<std::int16_t> World::next_entity(std::int16_t current,std::optional<EntityKind> kind,bool include_ui_ghost) const {
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it)
        if((include_ui_ghost||!it->second.ui_ghost)&&(!kind || it->second.kind==*kind))return it->first;
    return {};
}
std::optional<std::int16_t> World::next_entity_by_category(std::int16_t current,
                                                           std::int16_t category) const {
    constexpr std::int16_t category_word=59; // VMStackObjectVariable.Category
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it) {
        if(it->second.ui_ghost)continue;
        const auto value=it->second.object_words.find(category_word);
        if(value!=it->second.object_words.end()&&value->second==category)return it->first;
    }
    return {};
}
std::optional<std::int16_t> World::next_family_member(std::int16_t current) const {
    constexpr std::size_t family_word=61; // VMPersonDataVariable.TS1FamilyNumber
    const auto cursor=entities_.find(current);
    if(cursor==entities_.end()||cursor->second.ui_ghost||cursor->second.kind!=EntityKind::avatar||
       cursor->second.person_data.size()<=family_word)return {};
    const auto family=cursor->second.person_data[family_word];
    const auto matches=[&](const Entity& entity){
        return !entity.ui_ghost&&entity.kind==EntityKind::avatar&&
               entity.person_data.size()>family_word&&entity.person_data[family_word]==family;
    };
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it)
        if(matches(it->second))return it->first;
    for(const auto& [id,entity]:entities_)
        if(matches(entity))return id;
    return {};
}
std::optional<std::int16_t> World::next_entity_on_same_tile(std::int16_t current) const {
    const auto* anchor=find(current);
    if(!anchor||anchor->ui_ghost||!anchor->position||anchor->position->x<0||anchor->position->y<0||
       anchor->position->level<1||anchor->position->level>2)return {};
    const auto tile=[](std::int32_t coordinate){
        return coordinate>=0?coordinate/16:-static_cast<std::int32_t>((-static_cast<std::int64_t>(coordinate)+15)/16);
    };
    const auto same_tile=[&](const Entity& entity){
        if(!entity.position||entity.position->level!=anchor->position->level)return false;
        const auto& a=*anchor->position;const auto& b=*entity.position;
        return tile(a.x)==tile(b.x)&&tile(a.y)==tile(b.y);
    };
    std::optional<std::int16_t> first,after;
    bool anchor_present=false;
    for(const auto& [id,entity]:entities_){
        if(entity.ui_ghost)continue;
        if(!same_tile(entity))continue;
        if(!first)first=id;
        if(id==current)anchor_present=true;
        if(id>current&&!after)after=id;
    }
    if(after)return after;
    return anchor_present?first:std::nullopt;
}
std::optional<std::int16_t> World::next_entity_adjacent_to_local(std::int16_t anchor_id,
                                                                  std::int16_t current) const {
    const auto* anchor=find(anchor_id);
    if(!anchor||anchor->ui_ghost||!anchor->position||anchor->position->level<1||
       anchor->position->level>2)return {};
    const auto tile=[](std::int32_t coordinate){
        return coordinate>=0?coordinate/16:-static_cast<std::int32_t>(
            (-static_cast<std::int64_t>(coordinate)+15)/16);
    };
    const auto anchor_x=tile(anchor->position->x),anchor_y=tile(anchor->position->y);
    int previous=-1;
    if(const auto* pointer=find(current);pointer&&!pointer->ui_ghost&&pointer->position){
        const auto dx=tile(pointer->position->x)-anchor_x;
        const auto dy=tile(pointer->position->y)-anchor_y;
        previous=dx==0?(dy<0?0:2):(dx<0?3:1);
    }
    if(previous==3)return {};
    constexpr std::array<std::array<std::int32_t,2>,4> steps{{
        {{0,-1}},{{1,0}},{{0,1}},{{-1,0}}
    }};
    for(int direction=previous+1;direction<4;++direction){
        const auto x=anchor_x+steps[static_cast<std::size_t>(direction)][0];
        const auto y=anchor_y+steps[static_cast<std::size_t>(direction)][1];
        for(const auto& [id,entity]:entities_){
            if(entity.ui_ghost||!entity.position||entity.position->level!=anchor->position->level)continue;
            if(tile(entity.position->x)==x&&tile(entity.position->y)==y)return id;
        }
    }
    return {};
}
bool World::define_type(std::int16_t id,std::uint32_t guid,std::optional<std::uint32_t> master_guid) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);if(found==entities_.end())return false;
    auto& type=found->second.definition;
    if(type)return type->guid==guid&&type->master_guid==master_guid;
    type=EntityType{guid,master_guid};mark_spatial_change();return true;
}
bool World::define_entry_points(std::int16_t id,const std::vector<EntryPoint>& values) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end() || !found->second.definition || values.size()>256) return false;
    auto& table=found->second.entry_points;
    if(table) return table->size()==values.size() &&
        std::equal(table->begin(),table->end(),values.begin(),[](const auto& a,const auto& b) {
            return a.condition==b.condition && a.action==b.action;
        });
    // Divide limits before multiplying the bounded count, avoiding overflow.
    if(values.size()>limits_.bank_words/2 || values.size()>(limits_.words-words_)/2) return false;
    auto owned=values;
    table.emplace(std::move(owned));
    words_+=2*values.size();
    mark_spatial_change();
    return true;
}
bool World::declare_object_word(std::int16_t id, std::int16_t index, std::int16_t value) {
    if(reject_room_reentry())return false;
    const auto found = entities_.find(id);
    // Current TS1 reference bank; FSO's extension query 79 is outside this profile.
    if (found == entities_.end() || index < 0 || index >= 79 || derived(index)) return false;
    auto& bank = found->second.object_words;
    const auto field = bank.find(index);
    if (field != bank.end()) { if(field->second!=value){field->second = value;mark_spatial_change();} return true; }
    if (words_ >= limits_.words) return false;
    bank.emplace(index,value); ++words_;mark_spatial_change();return true;
}
bool World::set_person_data(std::int16_t id, std::int16_t index, std::int16_t value) {
    if(reject_room_reentry())return false;
    const auto found = entities_.find(id);
    if (found == entities_.end() || found->second.kind != EntityKind::avatar ||
        !person_data_value(found->second,index)) return false;
    if(index==8&&avatar_outfit_resolver_){
        std::optional<AvatarOutfitReference> outfit;
        if(!evaluate_appearance_lookup([&]{outfit=avatar_outfit_resolver_(*this,id,static_cast<std::uint16_t>(value));})||!outfit)return false;
        return set_avatar_outfit(id,*outfit,false,value);
    }
    auto& bank=found->second.person_data;
    const auto selected=static_cast<std::size_t>(index);
    if(selected>=bank.size()){
        const auto extent=logical_person_data_words(found->second),growth=extent-bank.size();
        if(extent>limits_.bank_words||growth>limits_.words-words_)return false;
        auto expanded=bank;expanded.resize(extent,0);expanded[selected]=value;
        bank.swap(expanded);words_+=growth;mark_spatial_change();return true;
    }
    auto& old=bank[selected];
    if(old!=value){old=value;mark_spatial_change();}return true;
}
bool World::replace_person_data(std::int16_t id, const std::vector<std::int16_t>& values) {
    if(reject_room_reentry())return false;
    const auto found = entities_.find(id);
    if (found == entities_.end() || found->second.kind != EntityKind::avatar ||
        values.size() > limits_.bank_words || values.size() > 32768) return false;
    auto& bank = found->second.person_data;
    const auto retained_words = words_ - bank.size();
    if (values.size() > limits_.words - retained_words) return false;
    const auto replacement_size = values.size();
    auto replacement = values; // Copy before publication, including self-aliasing input.
    bank.swap(replacement);
    words_ = retained_words + replacement_size;
    mark_spatial_change();
    return true;
}
bool World::add_bound_appearance(std::int16_t avatar,const std::string& name,std::optional<std::string> asset) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(avatar);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar||!valid_appearance_name(name)||
       (asset&&!valid_appearance_name(*asset)))return false;
    auto names=found->second.bound_appearances;const auto at=std::lower_bound(names.begin(),names.end(),name);
    if(at==names.end()||*at!=name)names.insert(at,name);
    std::size_t bytes=0;for(const auto& n:names)bytes+=n.size();
    if(names.size()>limits_.bound_appearance_names||bytes>limits_.bound_appearance_name_bytes)return false;
    auto state=found->second.avatar_appearance;
    if((asset&&*asset!=name)||(state&&state->accessory_assets)){
        auto resources=state&&state->accessory_assets?*state->accessory_assets:found->second.bound_appearances;
        const auto& resource=asset?*asset:name;const auto target=std::lower_bound(resources.begin(),resources.end(),resource);
        if(target==resources.end()||*target!=resource)resources.insert(target,resource);
        if((state&&state->accessory_assets)||resources!=names){if(!state)state=AvatarAppearanceState{};
            state->accessory_assets=std::move(resources);if(!valid_appearance_state(*state,limits_))return false;}
    }
    if(names!=found->second.bound_appearances||state!=found->second.avatar_appearance){
        found->second.bound_appearances.swap(names);found->second.avatar_appearance=std::move(state);mark_spatial_change();}
    return true;
}
bool World::remove_bound_appearance(std::int16_t avatar,const std::string& name,std::optional<std::string> asset) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(avatar);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar||!valid_appearance_name(name)||
       (asset&&!valid_appearance_name(*asset)))return false;
    auto names=found->second.bound_appearances;const auto at=std::lower_bound(names.begin(),names.end(),name);
    if(at!=names.end()&&*at==name)names.erase(at);
    auto state=found->second.avatar_appearance;
    if((asset&&*asset!=name)||(state&&state->accessory_assets)){
        auto resources=state&&state->accessory_assets?*state->accessory_assets:found->second.bound_appearances;
        const auto& resource=asset?*asset:name;const auto target=std::lower_bound(resources.begin(),resources.end(),resource);
        if(target!=resources.end()&&*target==resource)resources.erase(target);
        if((state&&state->accessory_assets)||resources!=names){if(!state)state=AvatarAppearanceState{};
            state->accessory_assets=std::move(resources);}
    }
    if(names!=found->second.bound_appearances||state!=found->second.avatar_appearance){
        found->second.bound_appearances.swap(names);found->second.avatar_appearance=std::move(state);mark_spatial_change();}
    return true;
}
bool World::set_name(WorldEntityReference ref,const std::string& name){
    if(reject_room_reentry()||!valid_entity_name(name))return false;
    auto live=entities_.find(ref.id);
    if(live!=entities_.end()&&live->second.generation==ref.generation){live->second.name_override=name;return true;}
    auto dead=retained_entities_.find(ref);
    if(dead==retained_entities_.end())return false;
    dead->second.name_override=name;return true;
}
bool World::clear_bound_appearances(std::int16_t id){
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);if(found==entities_.end()||found->second.kind!=EntityKind::avatar)return false;
    auto& entity=found->second;
    const bool changed=!entity.bound_appearances.empty()||(entity.avatar_appearance&&entity.avatar_appearance->accessory_assets&&
        !entity.avatar_appearance->accessory_assets->empty());
    entity.bound_appearances.clear();if(entity.avatar_appearance&&entity.avatar_appearance->accessory_assets)
        entity.avatar_appearance->accessory_assets->clear();
    if(changed)mark_spatial_change();
    return true;
}
bool World::publish_avatar_personal_states(const std::vector<AvatarPersonalStateChange>& changes) {
    if(reject_room_reentry()||changes.size()>limits_.entities)return false;
    struct Staged {Entity* entity;AvatarPersonalState state;};
    std::vector<Staged> staged;staged.reserve(changes.size());
    std::set<WorldEntityReference> distinct;bool appearance_changed=false;
    const auto same_attachment=[](const std::optional<Attachment>& a,const std::optional<Attachment>& b){
        return a.has_value()==b.has_value()&&(!a||(a->container==b->container&&a->slot==b->slot&&a->positioned==b->positioned));
    };
    for(const auto& change:changes){
        const auto found=entities_.find(change.reference.id);
        if(!distinct.insert(change.reference).second||!is_live_reference(change.reference)||found==entities_.end())return false;
        auto& entity=found->second;
        if(entity.kind!=EntityKind::avatar||entity.dead||entity.ui_ghost||
           entity.person_data!=change.expected.person_data||entity.motives!=change.expected.motives||
           entity.avatar_appearance!=change.expected.appearance||!(entity.position==change.expected_position)||
           !same_attachment(entity.attachment,change.expected_attachment)||
           change.replacement.person_data.size()!=entity.person_data.size()||
           change.replacement.motives.size()!=entity.motives.size()||
           (change.replacement.appearance&&!valid_appearance_state(*change.replacement.appearance,limits_)))return false;
        appearance_changed=appearance_changed||entity.avatar_appearance!=change.replacement.appearance;
        staged.push_back({&entity,change.replacement});
    }
    // No allocations or callbacks during this commit. Only personal banks
    // and outfit state change; World/cohort/physical ownership is never swapped.
    // Notify appearance observers only after the complete batch is committed.
    for(auto& item:staged){
        item.entity->person_data.swap(item.state.person_data);
        item.entity->motives.swap(item.state.motives);
        item.entity->avatar_appearance.swap(item.state.appearance);
    }
    if(appearance_changed)mark_spatial_change();
    return true;
}
bool World::set_motive(std::int16_t id, std::int16_t index, std::int16_t value) {
    if(reject_room_reentry())return false;
    const auto found = entities_.find(id);
    if (found == entities_.end() || found->second.kind != EntityKind::avatar ||
        !valid_index(index,found->second.motives.size())) return false;
    auto& old = found->second.motives[static_cast<std::size_t>(index)];
    const auto updated=std::clamp(value,static_cast<std::int16_t>(-100),
                     std::max(old,static_cast<std::int16_t>(100)));
    // Motives do not change room geometry or route obstacles.
    if(old!=updated)old=updated;
    return true;
}
namespace {
std::int16_t wrap_motive_word(std::int32_t value) {
    const auto bits=static_cast<std::uint16_t>(static_cast<std::uint32_t>(value));
    const auto signed_value=bits<0x8000U?static_cast<std::int32_t>(bits):
        static_cast<std::int32_t>(bits)-0x10000;
    return static_cast<std::int16_t>(signed_value);
}
std::int16_t wrap_motive_double(double value) {
    const auto integral=std::fmod(std::trunc(value),65536.0);
    const auto normalized=integral<0.0?integral+65536.0:integral;
    return wrap_motive_word(static_cast<std::int32_t>(normalized));
}
}
bool World::set_motive_change(std::int16_t id,std::int16_t motive,
                              std::int16_t per_hour_change,std::int16_t maximum) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar||
       !valid_index(motive,16)||found->second.motives.size()<16)return false;
    auto& changes=found->second.motive_changes;
    if(changes.empty())changes.resize(16);
    if(changes.size()!=16)return false;
    auto& change=changes[static_cast<std::size_t>(motive)];
    if(change.ticked){
        change.per_hour_change=per_hour_change;
        change.maximum=maximum;
        change.ticked=false;
    }
    return true;
}
bool World::clear_motive_changes(std::int16_t id) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end()||found->second.kind!=EntityKind::avatar)return false;
    for(auto& change:found->second.motive_changes){
        change.per_hour_change=0;
        change.maximum=INT16_MAX;
    }
    return true;
}
bool World::tick_motive_changes() {
    if(reject_room_reentry())return false;
    for(auto& [id,entity]:entities_){
        if(entity.kind!=EntityKind::avatar||entity.motives.size()<16)continue;
        if(entity.motive_changes.empty())entity.motive_changes.resize(16);
        if(entity.motive_changes.size()!=16)return false;
        for(std::size_t index=0;index<entity.motive_changes.size();++index){
            auto& change=entity.motive_changes[index];
            change.ticked=true;
            if(change.per_hour_change==0)continue;
            const auto rate=(static_cast<double>(change.per_hour_change)/60.0)/30.0;
            change.fractional+=rate;
            if(std::abs(change.fractional)<1.0)continue;
            auto& current=entity.motives[index];
            if((rate>0.0&&current>change.maximum)||(rate<0.0&&current<change.maximum))continue;
            const auto delta=wrap_motive_double(change.fractional);
            change.fractional=std::fmod(change.fractional,1.0);
            auto next=wrap_motive_word(static_cast<std::int32_t>(current)+delta);
            if((rate>0.0&&next>change.maximum)||(rate<0.0&&next<change.maximum))
                next=change.maximum;
            if(!set_motive(id,static_cast<std::int16_t>(index),next))return false;
        }
    }
    return true;
}
bool World::has_motive_change(std::int16_t id,std::int16_t motive) const {
    const auto found=entities_.find(id);
    return found!=entities_.end()&&found->second.kind==EntityKind::avatar&&
        valid_index(motive,found->second.motive_changes.size())&&
        found->second.motive_changes[static_cast<std::size_t>(motive)].per_hour_change!=0;
}
bool World::declare_definition_word(std::int16_t id, std::int16_t index, std::int16_t value) {
    if(reject_room_reentry())return false;
    const auto found = entities_.find(id);
    if (found == entities_.end() || index < 0 || index > 255) return false;
    auto& bank = found->second.definition_words;
    const auto field = bank.find(index);
    if (field != bank.end()) { if(field->second!=value)return false; return true; }
    if (bank.size() >= limits_.bank_words || words_ >= limits_.words) return false;
    bank.emplace(index,value); ++words_;mark_spatial_change();return true;
}
bool World::set_static_footprint_observer(StaticFootprintObserver observer){
    if(reject_room_reentry()||notifying_static_footprint_)return false;
    static_footprint_observer_=std::move(observer);return true;
}
bool World::set_static_footprint(std::int16_t id, bool is_static,FootprintClassChangeSource source) {
    if(reject_room_reentry()||notifying_static_footprint_)return false;
    if(source!=FootprintClassChangeSource::obstacle_static&&source!=FootprintClassChangeSource::flags_assignment)return false;
    const auto found=entities_.find(id);
    if(found==entities_.end()) return false;
    if(found->second.static_footprint!=is_static){
        const auto generation=found->second.generation;const auto old=found->second.static_footprint;
        found->second.static_footprint=is_static;mark_spatial_change();
        if(source==FootprintClassChangeSource::obstacle_static&&static_footprint_observer_){
            notifying_static_footprint_=true;struct Reset{bool& flag;~Reset(){flag=false;}}reset{notifying_static_footprint_};
            try{
                if(!static_footprint_observer_(*this,id,generation,old,is_static))return false;
                const auto* current=find(id);return current&&current->generation==generation&&current->static_footprint==is_static;
            }catch(...){return false;}
        }
    }
    return true;
}
bool World::set_footprint_observer(FootprintObserver observer){
    if(reject_room_reentry()||notifying_footprint_)return false;
    footprint_observer_=std::move(observer);return true;
}
bool World::notify_footprint_changed(std::int16_t id){
    if(reject_room_reentry()||notifying_footprint_)return false;
    const auto* e=find(id);if(!e)return false;
    if(!footprint_observer_)return true;
    const auto generation=e->generation;notifying_footprint_=true;struct Reset{bool& flag;~Reset(){flag=false;}}reset{notifying_footprint_};
    try{if(!footprint_observer_(*this,id,generation))return false;const auto* current=find(id);return current&&current->generation==generation;}catch(...){return false;}
}
bool World::set_ui_ghost(std::int16_t id,bool is_ghost) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end())return false;
    if(found->second.ui_ghost!=is_ghost){
        if(entity_visibility_revision_==std::numeric_limits<std::uint64_t>::max())return false;
        found->second.ui_ghost=is_ghost;++entity_visibility_revision_;mark_spatial_change();
    }
    return true;
}
std::optional<bool> World::dynamic_sprite_flag(std::int16_t id,std::uint16_t flag) const {
    const auto found=entities_.find(id);
    if(found==entities_.end())return std::nullopt;
    const auto bit=static_cast<unsigned>(flag&63U);
    return (found->second.dynamic_sprite_flags&(std::uint64_t{1}<<bit))!=0;
}
bool World::set_dynamic_sprite_flag(std::int16_t id,std::uint16_t flag,bool enabled) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end())return false;
    const auto bit=static_cast<unsigned>(flag&63U);
    const auto mask=std::uint64_t{1}<<bit;
    if(enabled)found->second.dynamic_sprite_flags|=mask;
    else found->second.dynamic_sprite_flags&=~mask;
    return true;
}
bool World::set_avatar_kill_timeout(std::int16_t id, std::int32_t value) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.kind!=EntityKind::avatar || value < -1) return false;
    found->second.kill_timeout=value;
    return true;
}
bool World::set_lockout_count_clock(std::uint32_t tick) noexcept {
    if(reject_room_reentry())return false;
    if(lockout_count_clock_&&*lockout_count_clock_==tick)return true;
    lockout_count_clock_=tick;
    if(slot_publication_epoch_!=std::numeric_limits<std::uint64_t>::max())++slot_publication_epoch_;
    return true;
}
bool World::admit_lockout_count_state(std::int16_t id,std::uint64_t generation,bool thread,
    std::optional<std::uint32_t> timestamp) noexcept {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(!lockout_count_clock_||found==entities_.end()||!generation||found->second.generation!=generation)return false;
    auto& state=found->second.lockout_count;
    if(state){
        if(state->thread_present!=thread)return false;
        if(state->timestamp)return !timestamp||timestamp==state->timestamp;
        if(!timestamp)return true;
        state->timestamp=timestamp;
    }else state=EntityLockoutCountState{thread,timestamp};
    if(slot_publication_epoch_!=std::numeric_limits<std::uint64_t>::max())++slot_publication_epoch_;
    return true;
}
std::vector<std::int16_t> World::position_tree(std::int16_t id) const {
    std::vector<std::int16_t> descendants{id};
    for(std::size_t n=0;n<descendants.size();++n)
        for(const auto occupant:entities_.at(descendants[n]).slots)
            if(occupant && entities_.at(occupant).attachment->positioned) descendants.push_back(occupant);
    return descendants;
}
std::vector<PreparedSlotMove::PositionUpdate> World::position_updates(
        const std::vector<std::int16_t>& descendants,EntityPosition position) const {
    std::vector<PreparedSlotMove::PositionUpdate> updates;updates.reserve(descendants.size());
    for(std::size_t n=0;n<descendants.size();++n){
        const auto& entity=entities_.at(descendants[n]);auto point=position;
        if(n)point.direction=entity.position?entity.position->direction:
            entity.kind==EntityKind::avatar?direction_notch(entity.radian_direction):
            static_cast<std::uint8_t>(entity.object_words.at(1));
        updates.push_back({entity.id,entity.generation,point,{},{}});
    }
    return updates;
}
bool World::stage_rooms(std::vector<PreparedSlotMove::PositionUpdate>& updates) const {
    if(reject_room_reentry())return false;
    if(!room_resolver_)return true;
    struct Guard {const World& world;explicit Guard(const World& value):world(value){
        world.resolving_rooms_=true;world.room_resolution_reentered_=false;}
        ~Guard(){world.resolving_rooms_=false;world.room_resolution_reentered_=false;}} guard(*this);
    const auto revision=room_capability_revision_;std::size_t missing=0;
    for(auto& update:updates){
        const auto* entity=find(update.id);
        if(!entity||entity->generation!=update.generation)return false;
        if(entity->ui_ghost)continue;
        std::optional<std::uint16_t> room=0;
        try{if(update.position)room=room_resolver_(*update.position);}catch(...){return false;}
        if(!room||*room>32768U||room_resolution_reentered_||room_capability_revision_!=revision)return false;
        update.room=static_cast<std::int16_t>(static_cast<std::int32_t>(*room)-1);
        if(!entity->object_words.count(29))++missing;
    }
    if(missing>limits_.words-words_)return false;
    // Finish all map allocation before coordinates, slots, words or counters change.
    for(auto& update:updates)if(update.room&&!entities_.at(update.id).object_words.count(29)){
        update.room_node=std::make_shared<std::map<std::int16_t,std::int16_t>>();
        update.room_node->emplace(29,*update.room);
    }
    return !room_resolution_reentered_;
}
void World::publish_room(PreparedSlotMove::PositionUpdate& update) noexcept {
    if(!update.room)return;
    auto& words=entities_.find(update.id)->second.object_words;
    if(update.room_node){words.insert(update.room_node->extract(29));++words_;}
    else words.find(29)->second=*update.room;
}
void World::apply_position(std::vector<PreparedSlotMove::PositionUpdate>& updates) noexcept {
    for(auto& update:updates){
        auto& entity=entities_.find(update.id)->second;publish_room(update);
        mark_room_registration(entity.id,entity.generation,entity.position,update.position);
        if(update.position||entity.position)entity.native_out_of_world_level.reset();
        entity.position=update.position;
        if(entity.kind==EntityKind::avatar&&direction_notch(entity.radian_direction)!=update.position->direction)
            entity.radian_direction=radians_for_notch(update.position->direction);
        if(auto direction=entity.object_words.find(1);direction!=entity.object_words.end())
            direction->second=update.position->direction;
    }
    mark_spatial_change();
}
bool World::set_position(std::int16_t id,EntityPosition position) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.attachment || !valid_position(position)) return false;
    const auto descendants=position_tree(id); // Allocation happens before any mutation.
    auto updates=position_updates(descendants,position);if(!stage_rooms(updates))return false;
    apply_position(updates);return true;
}
bool World::set_direction_publisher(DirectionPublisher publisher){
    if(reject_room_reentry()||publishing_direction_)return false;
    direction_publisher_=std::move(publisher);return true;
}
bool World::publish_native_object_heading(WorldEntityReference target,std::uint8_t direction){
    if(reject_room_reentry()||direction>7||!is_live_reference(target))return false;
    const auto* entity=resolve_reference(target);
    if(!entity||entity->kind!=EntityKind::object||entity->ui_ghost)return false;
    VmFrame frame;frame.context={target.id,target.id,target.id,0};
    frame.context.entity_references_bound=true;
    frame.context.caller_generation=frame.context.callee_generation=frame.context.stack_object_generation=target.generation;
    return write_direction(frame,target,direction,false,false);
}
bool World::write_direction(const VmFrame& frame,WorldEntityReference target,std::int16_t value,bool retained,bool quantize){
    if(publishing_direction_)return false;
    const auto* initial=resolve_reference(target);if(!initial)return false;
    const auto field=initial->object_words.find(1);if(field==initial->object_words.end())return false;
    const auto kind=initial->kind;const auto old_word=field->second;
    const auto old_position=initial->position;const auto old_attachment=initial->attachment;
    const auto old_radians=initial->radian_direction;
    // VMEntity.SetValue normalizes even negative shorts before virtual Direction.
    auto direction=static_cast<std::uint8_t>(static_cast<std::uint16_t>(value)%8U);
    std::optional<std::int16_t> notches;
    if(kind==EntityKind::object&&quantize){
        const auto found=initial->object_words.find(23);if(found==initial->object_words.end())return false;
        notches=found->second;
        if(*notches>1)direction=static_cast<std::uint8_t>(direction-direction%*notches);
    }
    const auto radians=kind==EntityKind::avatar?radians_for_notch(direction):old_radians;
    const bool changed=old_word!=direction||(old_position&&old_position->direction!=direction)||old_radians!=radians;
    if(!changed)return true;
    const auto same_attachment=[](const std::optional<Attachment>& a,const std::optional<Attachment>& b){
        return a.has_value()==b.has_value()&&(!a||(a->container==b->container&&a->slot==b->slot&&a->positioned==b->positioned));
    };
    const auto same_before=[this,target,kind,old_word,old_position,old_attachment,old_radians,notches,same_attachment](){
        const auto* current=resolve_reference(target);
        if(!current||current->kind!=kind||!(current->position==old_position)||
           !same_attachment(current->attachment,old_attachment)||current->radian_direction!=old_radians)return false;
        const auto field=current->object_words.find(1);if(field==current->object_words.end()||field->second!=old_word)return false;
        if(notches){const auto found=current->object_words.find(23);if(found==current->object_words.end()||found->second!=*notches)return false;}
        return true;
    };
    struct Lease {bool active=true,called=false,accepted=false,repeated=false;std::function<bool()> operation;};
    auto lease=std::make_shared<Lease>();
    lease->operation=[this,target,retained,direction,radians,same_before]{
        if(!same_before())return false;
        auto* current=const_cast<Entity*>(resolve_reference(target));
        current->object_words.at(1)=direction;
        if(current->position)current->position->direction=direction;
        if(current->kind==EntityKind::avatar)current->radian_direction=radians;
        // Source object Direction does not update its footprint or children.
        if(!retained)mark_spatial_change();return true;
    };
    struct End {std::shared_ptr<Lease> lease;bool& publishing;
        ~End(){lease->active=false;lease->operation={};publishing=false;}}end{lease,publishing_direction_};
    publishing_direction_=true;
    const std::function<bool()> fixed=[lease]{
        if(!lease->active)return false;
        if(lease->called){lease->repeated=true;return false;}
        lease->called=true;lease->accepted=lease->operation();return lease->accepted;
    };
    // Retained bodies no longer participate in the live spatial projection.
    // Their exact references still own writable state without touching ID reuse.
    const bool published=!retained&&direction_publisher_?direction_publisher_(*this,frame,target,direction,fixed):fixed();
    if(!published||!lease->called||!lease->accepted||lease->repeated)return false;
    const auto* after=retained?resolve_reference(target):find(target.id);
    if(!after||after->generation!=target.generation||after->kind!=kind||
       !same_attachment(after->attachment,old_attachment)||after->position.has_value()!=old_position.has_value()||
       after->radian_direction!=radians)return false;
    if(old_position&&(after->position->x!=old_position->x||after->position->y!=old_position->y||
       after->position->level!=old_position->level||after->position->direction!=direction))return false;
    const auto final_word=after->object_words.find(1);
    return final_word!=after->object_words.end()&&final_word->second==direction;
}
bool World::set_radian_direction(std::int16_t id,float radians) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.kind!=EntityKind::avatar || !std::isfinite(radians))
        return false;
    auto& avatar=found->second;
    const auto notch=direction_notch(radians);
    avatar.radian_direction=radians;
    if(avatar.position) avatar.position->direction=notch;
    if(auto direction=avatar.object_words.find(1);direction!=avatar.object_words.end())
        direction->second=notch;
    mark_spatial_change();
    return true;
}
bool World::place_on_floor(std::int16_t id,EntityPosition position) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(id);
    if(found==entities_.end() || !valid_position(position)) return false;
    const auto old=found->second.attachment;
    if(old) {
        const auto parent=entities_.find(old->container);
        if(parent==entities_.end() || old->slot>=parent->second.slots.size() ||
           parent->second.slots[old->slot]!=id) return false;
    }
    const auto descendants=position_tree(id);
    auto updates=position_updates(descendants,position);if(!stage_rooms(updates))return false;
    std::size_t room_words=0;for(const auto& update:updates)if(update.room_node)++room_words;
    std::map<std::int16_t,std::int16_t> native_nodes;
    if(old&&!stage_native_physical_parent(found->second,native_nodes,room_words))return false;
    if(old) {
        entities_.at(old->container).slots[old->slot]=0;
        found->second.attachment.reset();
        publish_native_physical_parent(found->second,native_nodes,-1,0);
        if (found->second.saved_object_banks) found->second.unattached_slot=-1;
    }
    apply_position(updates);return true;
}
bool World::place_out_of_world(std::int16_t id,std::uint8_t direction) {
    if(reject_room_reentry()||direction>7)return false;
    const auto found=entities_.find(id);if(found==entities_.end())return false;
    const auto old=found->second.attachment;
    if(old){const auto parent=entities_.find(old->container);
        if(parent==entities_.end()||old->slot>=parent->second.slots.size()||parent->second.slots[old->slot]!=id)return false;}
    const auto descendants=position_tree(id);
    std::vector<PreparedSlotMove::PositionUpdate> updates;updates.reserve(descendants.size());
    for(const auto child:descendants){const auto& entity=entities_.at(child);updates.push_back({child,entity.generation,{},{},{}});}
    if(!stage_rooms(updates))return false;
    std::vector<std::pair<std::int16_t,std::uint8_t>> directions;directions.reserve(descendants.size());
    std::vector<std::map<std::int16_t,std::int16_t>> staged_directions;staged_directions.reserve(descendants.size());
    for(const auto child:descendants){const auto& entity=entities_.at(child);
        const auto heading=child==id?direction:entity.position?entity.position->direction:
            entity.kind==EntityKind::avatar?direction_notch(entity.radian_direction):
            entity.object_words.count(1)?static_cast<std::uint8_t>(entity.object_words.at(1)):std::uint8_t{255};
        if(heading>7)return false;
        directions.emplace_back(child,heading);
        if(!entity.object_words.count(1)){staged_directions.emplace_back();staged_directions.back().emplace(1,heading);}
    }
    std::size_t extra_rooms=0;for(const auto& update:updates)if(update.room_node)++extra_rooms;
    if(extra_rooms>limits_.words-words_||staged_directions.size()>limits_.words-words_-extra_rooms)return false;
    std::map<std::int16_t,std::int16_t> native_nodes;
    if(old&&!stage_native_physical_parent(found->second,native_nodes,extra_rooms+staged_directions.size()))return false;
    // Allocation and all room/direction budgets are complete before detachment.
    if(old){entities_.at(old->container).slots[old->slot]=0;found->second.attachment.reset();
        publish_native_physical_parent(found->second,native_nodes,-1,0);
        if(found->second.saved_object_banks)found->second.unattached_slot=-1;}
    for(auto& update:updates){auto& entity=entities_.at(update.id);publish_room(update);
        if(entity.position)entity.native_out_of_world_level.reset();
        mark_room_registration(entity.id,entity.generation,entity.position,{});entity.position.reset();}
    std::size_t staged=0;
    for(const auto& heading:directions){auto& entity=entities_.at(heading.first);const auto word=entity.object_words.find(1);
        if(word==entity.object_words.end()){entity.object_words.insert(staged_directions[staged++].extract(1));++words_;}
        else word->second=heading.second;}
    if(found->second.kind==EntityKind::avatar)found->second.radian_direction=radians_for_notch(direction);
    mark_spatial_change();return true;
}
bool World::refresh_contained_position(std::int16_t child) {
    if(reject_room_reentry())return false;
    const auto found=entities_.find(child);
    if(found==entities_.end()||!found->second.attachment)return false;
    const auto attachment=*found->second.attachment;
    const auto parent=entities_.find(attachment.container);
    if(parent==entities_.end()||attachment.slot>=parent->second.slots.size()||parent->second.slots[attachment.slot]!=child)return false;
    auto& entity=found->second;
    const auto heading=entity.position?entity.position->direction:
        entity.kind==EntityKind::avatar?direction_notch(entity.radian_direction):
        entity.object_words.count(1)?static_cast<std::uint8_t>(entity.object_words.at(1)):std::uint8_t{255};
    if(heading>7)return false;
    std::optional<EntityPosition> position=parent->second.position;if(position)position->direction=heading;
    std::vector<PreparedSlotMove::PositionUpdate> updates{{child,entity.generation,position,{},{}}};
    if(!stage_rooms(updates))return false;
    const auto missing_direction=!entity.object_words.count(1);
    const auto extra_room=std::size_t(bool(updates.front().room_node));
    if(extra_room>limits_.words-words_||std::size_t(missing_direction)>limits_.words-words_-extra_room)return false;
    std::map<std::int16_t,std::int16_t> staged_direction;if(missing_direction)staged_direction.emplace(1,heading);
    auto& update=updates.front();publish_room(update);
    if(position)entity.native_out_of_world_level.reset();
    else entity.native_out_of_world_level=parent->second.native_out_of_world_level;
    mark_room_registration(entity.id,entity.generation,entity.position,position);entity.position=position;
    entity.attachment->positioned=true;
    if(missing_direction){entity.object_words.insert(staged_direction.extract(1));++words_;}
    else entity.object_words.find(1)->second=heading;
    // Preserve the child's continuous angle, including a nonquantized avatar.
    mark_spatial_change();return true;
}
bool World::place_individual_on_floor(std::int16_t id,std::optional<EntityPosition> position,std::uint8_t direction) {
    if(reject_room_reentry()||direction>7)return false;
    if(position){position->direction=direction;if(!valid_position(*position))return false;}
    const auto found=entities_.find(id);if(found==entities_.end())return false;
    const auto old=found->second.attachment;
    if(old){const auto parent=entities_.find(old->container);
        if(parent==entities_.end()||old->slot>=parent->second.slots.size()||parent->second.slots[old->slot]!=id)return false;}
    auto& entity=found->second;
    std::vector<PreparedSlotMove::PositionUpdate> updates{{id,entity.generation,position,{},{}}};
    if(!stage_rooms(updates))return false;
    const bool missing_direction=!entity.object_words.count(1);
    const auto extra_room=std::size_t(bool(updates.front().room_node));
    if(extra_room>limits_.words-words_||std::size_t(missing_direction)>limits_.words-words_-extra_room)return false;
    std::map<std::int16_t,std::int16_t> staged_direction;if(missing_direction)staged_direction.emplace(1,direction);
    std::map<std::int16_t,std::int16_t> native_nodes;
    if(old&&!stage_native_physical_parent(entity,native_nodes,extra_room+std::size_t(missing_direction)))return false;
    if(old){entities_.at(old->container).slots[old->slot]=0;entity.attachment.reset();
        publish_native_physical_parent(entity,native_nodes,-1,0);
        if(entity.saved_object_banks)entity.unattached_slot=-1;}
    if(position||entity.position)entity.native_out_of_world_level.reset();
    publish_room(updates.front());mark_room_registration(entity.id,entity.generation,entity.position,position);entity.position=position;
    if(missing_direction){entity.object_words.insert(staged_direction.extract(1));++words_;}
    else entity.object_words.find(1)->second=direction;
    if(entity.kind==EntityKind::avatar)entity.radian_direction=radians_for_notch(direction);
    mark_spatial_change();return true;
}
bool World::publish_native_recovery_position(WorldEntityReference reference,std::optional<EntityPosition> position,bool clear_slot){
    if(reject_room_reentry()||!is_live_reference(reference))return false;
    auto& entity=entities_.at(reference.id);
    if(entity.dead||entity.ui_ghost||(clear_slot&&entity.attachment))return false;
    const auto heading=entity.object_words.find(1);
    if(heading==entity.object_words.end()||heading->second<0||heading->second>7||
       (position&&(!valid_position(*position)||position->direction!=heading->second)))return false;
    std::vector<PreparedSlotMove::PositionUpdate> updates{{entity.id,entity.generation,position,{},{}}};
    if(!stage_rooms(updates))return false;
    // No script or rendering observer can see a transient snapped angle.
    if(clear_slot)entity.unattached_slot=-1;
    publish_room(updates.front());mark_room_registration(entity.id,entity.generation,entity.position,position);
    entity.native_out_of_world_level=position?std::nullopt:std::optional<std::int16_t>{1};
    entity.position=position;
    mark_spatial_change();return true;
}
bool World::stage_native_physical_parent(const Entity& entity,
    std::map<std::int16_t,std::int16_t>& nodes,std::size_t extra_words) const {
    if(!entity.native_slot0)return extra_words<=limits_.words-words_;
    if(entity.attachment){
        if(entity.attachment->slot>=32767)return false;
        const auto slot=entity.object_words.find(14),parent=entity.object_words.find(26);
        if((slot!=entity.object_words.end()&&slot->second!=std::int32_t(entity.attachment->slot)+1)||
           (parent!=entity.object_words.end()&&parent->second!=entity.attachment->container))return false;
    }
    for(const auto index:{std::int16_t{14},std::int16_t{26}})
        if(!entity.object_words.count(index))nodes.emplace(index,entity.attachment?
            (index==14?static_cast<std::int16_t>(entity.attachment->slot+1):entity.attachment->container):
            (index==14?std::int16_t{-1}:std::int16_t{0}));
    // Both indices are in the fixed TS1 ObjectData bank, independently of the
    // configurable attribute/person/slot bank_words limit (declare_object_word).
    return extra_words<=limits_.words-words_&&nodes.size()<=limits_.words-words_-extra_words;
}
void World::publish_native_physical_parent(Entity& entity,
    std::map<std::int16_t,std::int16_t>& nodes,std::int16_t slot,std::int16_t parent) noexcept {
    if(!entity.native_slot0)return;
    words_+=nodes.size();entity.object_words.merge(nodes);
    entity.object_words.find(14)->second=slot;entity.object_words.find(26)->second=parent;
}
bool World::stage_physical_detaches(const std::vector<std::int16_t>& ids,PhysicalDetachNodes& staged) const {
    std::size_t added=0;staged.reserve(ids.size());
    for(const auto id:ids){
        const auto* child=find(id);if(!child||!child->attachment)return false;
        if(std::any_of(staged.begin(),staged.end(),[&](const auto& row){return row.first==id;}))return false;
        const auto parent=attachment_parent({id,child->generation});if(!parent)return false;
        staged.emplace_back(id,std::map<std::int16_t,std::int16_t>{});
        if(!stage_native_physical_parent(*child,staged.back().second,added))return false;
        added+=staged.back().second.size();
    }
    return true;
}
void World::publish_physical_detach_nodes(PhysicalDetachNodes& staged) noexcept {
    // Nodes carry the CURRENT physical relation. Publish allocations together,
    // then ordinary detach can clear each relation without allocating/failing
    // after earlier siblings have already been detached.
    for(auto& row:staged){words_+=row.second.size();entities_.find(row.first)->second.object_words.merge(row.second);}
}
bool World::stage_slot_growth(const Entity& container,std::size_t slot,std::vector<std::int16_t>& expanded,
        std::size_t extra_words) const {
    if(slot>=logical_containment_slots(container))return false;
    if(extra_words>limits_.words-words_)return false;
    if(slot<container.slots.size())return true;
    const auto extent=logical_containment_slots(container);
    const auto growth=extent-container.slots.size();
    if(extent>limits_.bank_words||growth>limits_.words-words_-extra_words)return false;
    expanded=container.slots;expanded.resize(extent,0);return true;
}
bool World::place_in_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                          std::optional<std::uint8_t> direction) {
    if(reject_room_reentry())return false;
    const auto c=entities_.find(container),o=entities_.find(occupant);
    if(c==entities_.end() || o==entities_.end() || !c->second.position || !o->second.position ||
       slot>=logical_containment_slots(c->second) || (direction&&*direction>7)) return false;
    const bool same=containment_slot_value(c->second,slot)==std::optional<std::int16_t>{occupant} && o->second.attachment &&
                    o->second.attachment->container==container && o->second.attachment->slot==slot;
    const auto descendants=position_tree(occupant);
    auto p=*c->second.position;p.direction=direction.value_or(o->second.position->direction);
    auto updates=position_updates(descendants,p);if(!stage_rooms(updates))return false;
    std::size_t room_words=0;for(const auto& update:updates)if(update.room_node)++room_words;
    std::map<std::int16_t,std::int16_t> native_nodes;
    if(!same&&!stage_native_physical_parent(o->second,native_nodes,room_words))return false;
    std::vector<std::int16_t> expanded;
    if(!stage_slot_growth(c->second,slot,expanded,room_words+native_nodes.size()))return false;
    if(!same && !attach(container,slot,occupant)) return false;
    apply_position(updates);o->second.attachment->positioned=true;return true;
}
bool World::move_to_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                         std::optional<std::uint8_t> direction) {
    if(reject_room_reentry())return false;
    const auto c=entities_.find(container),o=entities_.find(occupant);
    if(c==entities_.end()||o==entities_.end()||container==occupant||!c->second.position||
       !o->second.position||slot>=logical_containment_slots(c->second)||(direction&&*direction>7))return false;
    const auto value=containment_slot_value(c->second,slot);
    if(value==std::optional<std::int16_t>{occupant}&&o->second.attachment&&
       o->second.attachment->container==container&&o->second.attachment->slot==slot)return true;
    if(value!=std::optional<std::int16_t>{0})return false;
    const WorldEntityReference native_child{occupant,o->second.generation};
    if(native_hierarchy_would_cycle({container,c->second.generation},native_child))return false;
    for(const auto& item:entities_)if(item.second.native_slot0&&*item.second.native_slot0==native_child)return false;
    if(o->second.native_slot0&&slot>=32767)return false;
    const auto previous=o->second.attachment;
    if(previous){
        const auto source=entities_.find(previous->container);
        if(source==entities_.end()||previous->slot>=source->second.slots.size()||source->second.slots[previous->slot]!=occupant)return false;
    }
    const Entity* ancestor=&c->second;
    while(ancestor->attachment){
        if(ancestor->attachment->container==occupant)return false;
        ancestor=find(ancestor->attachment->container);if(!ancestor)return false;
    }
    const auto descendants=position_tree(occupant);
    auto position=*c->second.position;position.direction=direction.value_or(o->second.position->direction);
    auto updates=position_updates(descendants,position);if(!stage_rooms(updates))return false;
    std::size_t room_words=0;for(const auto& update:updates)if(update.room_node)++room_words;
    std::map<std::int16_t,std::int16_t> native_nodes;
    if(!stage_native_physical_parent(o->second,native_nodes,room_words))return false;
    std::vector<std::int16_t> expanded;
    if(!stage_slot_growth(c->second,slot,expanded,room_words+native_nodes.size()))return false;
    // All allocations and both sides' validation finish before detaching.
    if(!expanded.empty()){words_+=expanded.size()-c->second.slots.size();c->second.slots.swap(expanded);}
    if(previous){entities_.at(previous->container).slots[previous->slot]=0;
        if(o->second.saved_object_banks)o->second.unattached_slot=-1;
    }
    c->second.slots[slot]=occupant;o->second.attachment=Attachment{container,slot,true};
    publish_native_physical_parent(o->second,native_nodes,static_cast<std::int16_t>(slot+1),container);
    mark_spatial_change();
    apply_position(updates);return true;
}
std::optional<PreparedSlotMove> World::prepare_move_to_slot(std::int16_t container,
    std::size_t slot,std::int16_t occupant) const {
    const auto c=entities_.find(container),o=entities_.find(occupant);
    if(c==entities_.end()||o==entities_.end()||container==occupant||
       c->second.ui_ghost||o->second.ui_ghost||slot>=logical_containment_slots(c->second)||
       spatial_revision_==std::numeric_limits<std::uint64_t>::max()||
       slot_publication_epoch_==std::numeric_limits<std::uint64_t>::max())return {};
    const WorldEntityReference native_child{occupant,o->second.generation};
    if(native_hierarchy_would_cycle({container,c->second.generation},native_child))return {};
    for(const auto& item:entities_)if(item.second.native_slot0&&*item.second.native_slot0==native_child)return {};
    PreparedSlotMove plan;
    plan.world_=this;plan.spatial_revision_=spatial_revision_;plan.generation_sequence_=generation_sequence_;
    plan.room_capability_revision_=room_capability_revision_;
    plan.publication_epoch_=slot_publication_epoch_;plan.container_=container;plan.occupant_=occupant;
    plan.slot_=slot;plan.container_generation_=c->second.generation;plan.occupant_generation_=o->second.generation;
    plan.native_parent_known_=o->second.native_slot0.has_value();
    if(plan.native_parent_known_&&slot>=32767)return {};
    plan.source_=o->second.attachment;
    if(plan.source_){
        const auto source=entities_.find(plan.source_->container);
        if(source==entities_.end()||source->second.ui_ghost||plan.source_->slot>=source->second.slots.size()||
           source->second.slots[plan.source_->slot]!=occupant)return {};
        plan.source_generation_=source->second.generation;
    }
    plan.already_=containment_slot_value(c->second,slot)==std::optional<std::int16_t>{occupant}&&plan.source_&&
        plan.source_->container==container&&plan.source_->slot==slot;
    if(plan.already_)return plan;
    if(containment_slot_value(c->second,slot)!=std::optional<std::int16_t>{0})return {};
    const Entity* ancestor=&c->second;std::size_t depth=0;
    while(ancestor){
        if(ancestor->id==occupant||ancestor->ui_ghost||++depth>entities_.size())return {};
        if(!ancestor->attachment)break;
        const auto parent=entities_.find(ancestor->attachment->container);
        if(parent==entities_.end()||ancestor->attachment->slot>=parent->second.slots.size()||
           parent->second.slots[ancestor->attachment->slot]!=ancestor->id)return {};
        ancestor=&parent->second;
    }
    std::vector<std::int16_t> pending{occupant};
    plan.updates_.reserve(entities_.size());pending.reserve(entities_.size());
    for(std::size_t n=0;n<pending.size();++n){
        const auto found=entities_.find(pending[n]);
        if(found==entities_.end()||found->second.ui_ghost)return {};
        const auto& entity=found->second;std::uint8_t direction=0;
        if(entity.kind==EntityKind::avatar){
            if(!std::isfinite(entity.radian_direction))return {};
            direction=direction_notch(entity.radian_direction);
            if(entity.position&&entity.position->direction!=direction)return {};
        }else if(entity.position)direction=entity.position->direction;
        else {
            const auto stored=entity.object_words.find(1);
            if(stored==entity.object_words.end()||stored->second<0||stored->second>7)return {};
            direction=static_cast<std::uint8_t>(stored->second);
        }
        if(entity.kind==EntityKind::object){
            const auto stored=entity.object_words.find(1);
            if((!c->second.position&&stored==entity.object_words.end())||
               (stored!=entity.object_words.end()&&stored->second!=direction))return {};
        }
        auto position=c->second.position;if(position)position->direction=direction;
        plan.updates_.push_back({entity.id,entity.generation,position});
        for(std::size_t child_slot=0;child_slot<entity.slots.size();++child_slot){
            const auto child_id=entity.slots[child_slot];if(!child_id)continue;
            const auto child=entities_.find(child_id);
            if(child==entities_.end()||!child->second.attachment||
               child->second.attachment->container!=entity.id||child->second.attachment->slot!=child_slot||
               std::find(pending.begin(),pending.end(),child_id)!=pending.end()||pending.size()>=entities_.size())return {};
            pending.push_back(child_id);
        }
    }
    if(!stage_rooms(plan.updates_))return {};
    std::size_t room_words=0;for(const auto& update:plan.updates_)if(update.room_node)++room_words;
    if(!stage_native_physical_parent(o->second,plan.native_parent_nodes_,room_words))return {};
    if(!stage_slot_growth(c->second,slot,plan.expanded_slots_,room_words+plan.native_parent_nodes_.size()))return {};
    return plan;
}
bool World::can_commit_move_to_slot(const PreparedSlotMove& plan) const noexcept {
    if(plan.world_!=this||plan.consumed_||plan.room_capability_revision_!=room_capability_revision_||plan.spatial_revision_!=spatial_revision_||
       plan.generation_sequence_!=generation_sequence_||plan.publication_epoch_!=slot_publication_epoch_)return false;
    const auto c=entities_.find(plan.container_),o=entities_.find(plan.occupant_);
    if(c==entities_.end()||o==entities_.end()||c->second.generation!=plan.container_generation_||
       o->second.generation!=plan.occupant_generation_||c->second.ui_ghost||o->second.ui_ghost||
       plan.slot_>=logical_containment_slots(c->second)||bool(o->second.attachment)!=bool(plan.source_))return false;
    if(o->second.native_slot0.has_value()!=plan.native_parent_known_)return false;
    const WorldEntityReference native_child{plan.occupant_,plan.occupant_generation_};
    if(native_hierarchy_would_cycle({plan.container_,plan.container_generation_},native_child))return false;
    for(const auto& item:entities_)if(item.second.native_slot0&&*item.second.native_slot0==native_child)return false;
    if(plan.source_){
        const auto& current=*o->second.attachment;const auto& old=*plan.source_;
        if(current.container!=old.container||current.slot!=old.slot||current.positioned!=old.positioned)return false;
        const auto source=entities_.find(old.container);
        if(source==entities_.end()||source->second.generation!=plan.source_generation_||
           old.slot>=source->second.slots.size()||source->second.slots[old.slot]!=plan.occupant_)return false;
    }
    if(plan.already_)return c->second.slots[plan.slot_]==plan.occupant_;
    if(containment_slot_value(c->second,plan.slot_)!=std::optional<std::int16_t>{0}||plan.updates_.empty())return false;
    std::size_t growth=0;
    if(plan.slot_>=c->second.slots.size()){
        if(plan.expanded_slots_.size()!=logical_containment_slots(c->second)||
           !std::equal(c->second.slots.begin(),c->second.slots.end(),plan.expanded_slots_.begin())||
           !std::all_of(plan.expanded_slots_.begin()+static_cast<std::ptrdiff_t>(c->second.slots.size()),plan.expanded_slots_.end(),[](auto value){return value==0;}))return false;
        growth=plan.expanded_slots_.size()-c->second.slots.size();
    }else if(!plan.expanded_slots_.empty())return false;
    for(const auto& update:plan.updates_)if(update.room_node)++growth;
    if(plan.native_parent_known_){
        std::size_t missing=0;
        for(const auto index:{std::int16_t{14},std::int16_t{26}}){
            const auto stored=o->second.object_words.find(index);
            if(stored==o->second.object_words.end()){
                ++missing;if(!plan.native_parent_nodes_.count(index))return false;
            }else if(plan.native_parent_nodes_.count(index))return false;
            else if(plan.source_&&stored->second!=(index==14?std::int32_t(plan.source_->slot)+1:plan.source_->container))return false;
        }
        if(missing!=plan.native_parent_nodes_.size())return false;
    }else if(!plan.native_parent_nodes_.empty())return false;
    growth+=plan.native_parent_nodes_.size();
    if(growth>limits_.words-words_)return false;
    for(const auto& update:plan.updates_){const auto node=entities_.find(update.id);
        if(node==entities_.end()||node->second.generation!=update.generation||node->second.ui_ghost)return false;
        if(update.room_node&&(update.room_node->size()!=1||!update.room_node->count(29)||node->second.object_words.count(29)))return false;
    }
    return true;
}
bool World::commit_move_to_slot(PreparedSlotMove& plan) noexcept {
    if(reject_room_reentry())return false;
    if(!can_commit_move_to_slot(plan))return false;
    plan.consumed_=true;if(plan.already_)return true;
    auto& container=entities_.find(plan.container_)->second;
    auto& occupant=entities_.find(plan.occupant_)->second;
    if(!plan.expanded_slots_.empty()){words_+=plan.expanded_slots_.size()-container.slots.size();container.slots.swap(plan.expanded_slots_);}
    if(plan.source_){
        entities_.find(plan.source_->container)->second.slots[plan.source_->slot]=0;
        if(occupant.saved_object_banks)occupant.unattached_slot=-1;
    }
    container.slots[plan.slot_]=plan.occupant_;
    occupant.attachment=Attachment{plan.container_,plan.slot_,true};
    publish_native_physical_parent(occupant,plan.native_parent_nodes_,static_cast<std::int16_t>(plan.slot_+1),plan.container_);
    for(auto& update:plan.updates_){
        auto& entity=entities_.find(update.id)->second;publish_room(update);
        mark_room_registration(entity.id,entity.generation,entity.position,update.position);
        if(update.position||entity.position)entity.native_out_of_world_level.reset();
        entity.position=update.position;
        if(entity.attachment)entity.attachment->positioned=true;
    }
    mark_spatial_change();return true;
}
bool World::attach(std::int16_t container, std::size_t slot, std::int16_t occupant) {
    if(reject_room_reentry())return false;
    const auto c = entities_.find(container), o = entities_.find(occupant);
    if (c == entities_.end() || o == entities_.end() || container == occupant ||
        slot >= logical_containment_slots(c->second) || containment_slot_value(c->second,slot) != std::optional<std::int16_t>{0} || o->second.attachment) return false;
    const WorldEntityReference native_child{occupant,o->second.generation};
    if(native_hierarchy_would_cycle({container,c->second.generation},native_child))return false;
    for(const auto& item:entities_)if(item.second.native_slot0&&*item.second.native_slot0==native_child)return false;
    // Reject containment cycles before changing either side of the relation.
    const Entity* ancestor = &c->second;
    while (ancestor->attachment) {
        const auto parent = ancestor->attachment->container;
        if (parent == occupant) return false;
        ancestor = find(parent);
        if (!ancestor) return false;
    }
    if(o->second.native_slot0&&slot>=32767)return false;
    std::map<std::int16_t,std::int16_t> added;
    if(!stage_native_physical_parent(o->second,added))return false;
    std::vector<std::int16_t> expanded;
    if(!stage_slot_growth(c->second,slot,expanded,added.size()))return false;
    if(!expanded.empty()){words_+=expanded.size()-c->second.slots.size();c->second.slots.swap(expanded);}
    c->second.slots[slot] = occupant;
    o->second.attachment = Attachment{container,slot};
    publish_native_physical_parent(o->second,added,static_cast<std::int16_t>(slot+1),container);
    mark_spatial_change();
    return true;
}
bool World::detach(std::int16_t container, std::size_t slot) {
    if(reject_room_reentry())return false;
    const auto c = entities_.find(container);
    if (c == entities_.end() || slot >= logical_containment_slots(c->second)) return false;
    if(slot>=c->second.slots.size())return true; // Proven empty legacy avatar suffix; no mutation.
    const auto occupant = c->second.slots[slot];
    if (occupant) {
        auto& entity=entities_.at(occupant);
        if(!entity.attachment||entity.attachment->container!=container||entity.attachment->slot!=slot)return false;
        std::map<std::int16_t,std::int16_t> added;
        if(!stage_native_physical_parent(entity,added))return false;
        entity.attachment.reset();
        if (entity.saved_object_banks) entity.unattached_slot=-1;
        publish_native_physical_parent(entity,added,-1,0);
    }
    c->second.slots[slot] = 0;
    if(occupant)mark_spatial_change();
    return true;
}
bool World::retire(std::int16_t id) {
    if(reject_room_reentry())return false;
    if(id<=0 || !entities_.count(id) || limits_.reserved_entity_ids.size()>=4096)return false;
    auto reserved=limits_.reserved_entity_ids;
    reserved.insert(std::lower_bound(reserved.begin(),reserved.end(),id),id);
    // World containment is reciprocal and validated by every public mutation;
    // remove/detach perform no allocation once the reservation is prepared.
    if(!remove(id))return false;
    limits_.reserved_entity_ids.swap(reserved);return true;
}
bool World::remove(std::int16_t id) {
    if(reject_room_reentry())return false;
    const auto* native_body=find(id);
    if(!native_body||!native_slot0_detached({id,native_body->generation}))return false;
    const auto found = entities_.find(id);
    if (found == entities_.end()) return false;
    auto& entity = found->second;
    std::vector<std::int16_t> detach_ids;if(entity.attachment)detach_ids.push_back(id);
    for(std::size_t slot=0;slot<entity.slots.size();++slot)if(entity.slots[slot]){
        const auto* child=find(entity.slots[slot]);
        if(!child||!child->attachment||child->attachment->container!=id||child->attachment->slot!=slot)return false;
        detach_ids.push_back(child->id);
    }
    PhysicalDetachNodes native_nodes;if(!stage_physical_detaches(detach_ids,native_nodes))return false;
    publish_physical_detach_nodes(native_nodes);
    if(entity.attachment){
        const auto parent=attachment_parent({id,entity.generation});if(!parent)return false;
        if(is_live_reference(*parent)){if(!detach(parent->id,entity.attachment->slot))return false;}
        else {auto& body=retained_entities_.at(*parent);body.slots[entity.attachment->slot]=0;
            entity.attachment.reset();if(entity.saved_object_banks)entity.unattached_slot=-1;
            std::map<std::int16_t,std::int16_t> empty;publish_native_physical_parent(entity,empty,-1,0);mark_spatial_change();}
    }
    for (std::size_t slot = 0; slot < entity.slots.size(); ++slot)if(!detach(id,slot))return false;
    words_ -= entity.attributes.size() + entity.person_data.size() + entity.slots.size() +
              entity.object_words.size() + entity.motives.size() + entity.definition_words.size() +
              (entity.entry_points ? 2*entity.entry_points->size() : 0) +
              (entity.saved_object_banks ? 73 : 0);
    unlink_original_load({id,entity.generation});
    entities_.erase(found);mark_spatial_change();return true;
}
std::unique_ptr<World> World::clone_for_check() const {
    auto clone=std::make_unique<World>(limits_,attribute_policy_);
    if(!clone->restore_snapshot(snapshot()))throw std::invalid_argument("read-only World clone rejected source snapshot");
    clone->definition_word_resolver_=definition_word_resolver_;
    clone->room_resolver_=room_resolver_;
    clone->avatar_outfit_resolver_=avatar_outfit_resolver_;
    clone->current_value_read_resolver_=current_value_read_resolver_;
    return clone;
}

std::optional<std::int16_t> World::read(const VmFrame& frame, std::uint8_t scope, std::int16_t index) const {
    return read_impl(frame,scope,index,true);
}
std::optional<std::int16_t> World::read_only(const VmFrame& frame,std::uint8_t scope,std::int16_t index) const {
    return read_impl(frame,scope,index,false);
}
std::optional<std::int16_t> World::read_impl(const VmFrame& frame,std::uint8_t scope,std::int16_t index,bool commit_logical_reads) const {
    // FreeSO7e76 VMMemory: NeighborhoodData is an unconditional zero read,
    // including absent entities and any signed index. Original tutorial state
    // is not implemented by this reference compatibility contract.
    if(scope==34)return std::int16_t{0};
    if((resolving_rooms_||resolving_outfit_)&&(scope==3||scope==4)&&index==25){reject_room_reentry();return {};}

    if (scope != 0 && scope != 1 && scope != 3 && scope != 4 && scope != 14 && scope != 15 &&
        scope != 16 && scope != 18 && scope != 19 && scope != 20 && scope != 21 && scope != 35) return std::nullopt;
    const auto* entity = frame_entity(frame,scope_role(scope));
    if (!entity) return std::nullopt;
    if (scope == 35) {
        if (!entity->entry_points || !valid_index(index,entity->entry_points->size())) return std::nullopt;
        const auto action=(*entity->entry_points)[static_cast<std::size_t>(index)].action;
        // Preserve the low 16 bits with a defined signed conversion.
        return static_cast<std::int16_t>(action<=32767 ? static_cast<std::int32_t>(action)
                                                     : static_cast<std::int32_t>(action)-65536);
    }
    if (scope == 21) {
        if(index<0 || index>255) return std::nullopt;
        const auto field = entity->definition_words.find(index);
        if(field!=entity->definition_words.end())return field->second;
        return definition_word_resolver_ ? definition_word_resolver_(*entity,index) : std::nullopt;
    }
    if (scope == 14 || scope == 15) {
        if (entity->kind != EntityKind::avatar || !valid_index(index,entity->motives.size())) return std::nullopt;
        return entity->motives[static_cast<std::size_t>(index)];
    }
    if (scope == 0 || scope == 1) {
        if (!valid_index(index,entity->attributes.size())) {
            if (attribute_policy_ == AttributePolicy::ts1_bounded &&
                valid_index(index,limits_.bank_words)) return std::int16_t{0};
            return std::nullopt;
        }
        return entity->attributes[static_cast<std::size_t>(index)];
    }
    if (scope == 18 || scope == 19) {
        if(entity->kind!=EntityKind::avatar)return std::nullopt;
        return person_data_value(*entity,index);
    }
    if (scope == 16 || scope == 20) {
        if(index<0)return std::nullopt;
        return containment_slot_value(*entity,static_cast<std::size_t>(index));
    }
    if (index == 11) return entity->id;
    if (index == 67) return static_cast<std::int16_t>(logical_containment_slots(*entity));
    // Pinned VMEntity.GetValue aliases ParentId26 to current ContainerId2.
    // SetValue still stores raw26: retain that shadow and saved banks for exact
    // old checkpoint compatibility, but never use them as the live parent.
    if (index == 2 || index == 26) return entity->attachment ? entity->attachment->container : static_cast<std::int16_t>(0);
    if (index == 3) return entity->attachment ? static_cast<std::int16_t>(entity->attachment->slot) : entity->unattached_slot;
    // Simitone's VMEntity.GetValue(Room) reports OUT_OF_WORLD (-5) until the
    // entity has a position, regardless of the stored ObjectData[29] word.
    if (index == 29 && !entity->position) return std::int16_t{-5};
    const auto field = entity->object_words.find(index);
    if(field!=entity->object_words.end()&&index==25&&lockout_count_clock_&&(scope==3||scope==4)){
        if(!entity->lockout_count||!entity->lockout_count->thread_present)return {};
        auto stamp=entity->lockout_count->timestamp;
        if(stamp&&*stamp>*lockout_count_clock_){
            stamp=0;
            if(commit_logical_reads){
                entity->lockout_count->timestamp=stamp;
                if(!retained_entities_.count({entity->id,entity->generation})&&slot_publication_epoch_!=std::numeric_limits<std::uint64_t>::max())++slot_publication_epoch_;
            }
        }
        if(field->second<=0)return std::int16_t{0};
        if(!stamp)return {};
        const auto elapsed=static_cast<std::uint32_t>(*lockout_count_clock_-*stamp);
        return static_cast<std::int16_t>(elapsed>=static_cast<std::uint32_t>(field->second)?0:
            static_cast<std::uint32_t>(field->second)-elapsed);
    }
    if(index==41&&(scope==3||scope==4)&&entity->kind==EntityKind::object&&
       current_value_read_resolver_&&!retained_entities_.count({entity->id,entity->generation})){
        if(resolving_current_value_){current_value_reentry_=true;return {};}
        resolving_current_value_=true;current_value_reentry_=false;CurrentValueReadResult value;
        try{value=current_value_read_resolver_(*this,{entity->id,entity->generation});}catch(...){resolving_current_value_=false;return {};}
        resolving_current_value_=false;if(current_value_reentry_)return {};
        if(value.handled)return value.value;
    }
    return field == entity->object_words.end() ? std::nullopt : std::optional<std::int16_t>(field->second);
}
bool World::write(const VmFrame& frame, std::uint8_t scope, std::int16_t index, std::int16_t value) {
    if(reject_room_reentry())return false;
    auto* selected=frame_entity_mutable(frame,scope_role(scope));if(!selected)return false;
    auto& entity=*selected;
    // Indexed bodies still execute their ordinary callbacks during source Dead→Remove.
    const bool dead=retained_entities_.count({entity.id,entity.generation})!=0;
    if (scope == 0 || scope == 1) {
        if (!valid_index(index,entity.attributes.size())) {
            if (attribute_policy_ != AttributePolicy::ts1_bounded ||
                !valid_index(index,limits_.bank_words)) return false;
            const auto replacement_size = static_cast<std::size_t>(index) + 1;
            const auto retained_words = words_ - entity.attributes.size();
            if (replacement_size > limits_.words - retained_words) return false;
            auto replacement = entity.attributes;
            replacement.resize(replacement_size,0);
            replacement[static_cast<std::size_t>(index)] = value;
            entity.attributes.swap(replacement);
            words_ = retained_words + replacement_size;
            if(!dead)mark_spatial_change();
            return true;
        }
        auto& old=entity.attributes[static_cast<std::size_t>(index)];
        if(old!=value){old=value;if(!dead)mark_spatial_change();}
        return true;
    }
    if(scope==18||scope==19){
        if(!dead)return set_person_data(entity.id,index,value);
        if(entity.kind!=EntityKind::avatar||!person_data_value(entity,index)||(index==8&&avatar_outfit_resolver_))return false;
        const auto selected=static_cast<std::size_t>(index);
        if(selected>=entity.person_data.size()){
            const auto extent=logical_person_data_words(entity),growth=extent-entity.person_data.size();
            if(extent>limits_.bank_words||growth>limits_.words-words_)return false;
            entity.person_data.resize(extent,0);words_+=growth;
        }
        entity.person_data[selected]=value;return true;
    }
    if(scope==14||scope==15){
        if(!dead)return set_motive(entity.id,index,value);
        if(entity.kind!=EntityKind::avatar||!valid_index(index,entity.motives.size()))return false;
        auto& old=entity.motives[static_cast<std::size_t>(index)];old=std::clamp(value,std::int16_t{-100},std::max(old,std::int16_t{100}));return true;
    }
    if ((scope == 3 || scope == 4) && !derived(index)) {
        const auto field = entity.object_words.find(index);
        if (field == entity.object_words.end()) return false;
        if(index==25&&lockout_count_clock_){
            if(!entity.lockout_count)return false;
            if(entity.lockout_count->thread_present){
                auto& stamp=entity.lockout_count->timestamp;
                if(stamp!=lockout_count_clock_){
                    stamp=*lockout_count_clock_;
                    if(slot_publication_epoch_!=std::numeric_limits<std::uint64_t>::max())++slot_publication_epoch_;
                }
            }
        }
        if(index==1)return write_direction(frame,{entity.id,entity.generation},value,dead);
        if(index==41&&!dead&&entity.kind==EntityKind::object&&current_value_write_observer_)
            return write_current_value({entity.id,entity.generation},field->second,value);
        if(dead&&index==8&&((std::uint16_t(field->second)^std::uint16_t(value))&4U))return false;
        if(field->second!=value){
            const auto old=field->second;const auto id=entity.id;const auto generation=entity.generation;
            field->second=value;if(!dead)mark_spatial_change();
            if(index==8&&dead)entity.static_footprint=(std::uint16_t(value)&0x8000U)==0;
            if(index==8&&!dead){
                const auto changed=std::uint16_t(old)^std::uint16_t(value);
                // Source SetValue(Flags): publish ObjectData first; UpdateFootprint
                // sees the previous StaticFootprint; only then assign the
                // dynamic-class property directly (not SetObstacleStatic).
                if((changed&4U)&&!notify_footprint_changed(id))return false;
                const auto* current=find(id);if(!current||current->generation!=generation)return false;
                if((changed&0x8000U)&&!set_static_footprint(id,(std::uint16_t(value)&0x8000U)==0,FootprintClassChangeSource::flags_assignment))return false;
            }
        }
        return true;
    }
    return false;
}
bool World::merge_attributes_max(WorldEntityReference reference,const std::int16_t* values,std::size_t count) {
    if(reject_room_reentry()||(!values&&count)||reference.id<=0||!reference.generation)return false;
    const auto found=entities_.find(reference.id);
    if(found==entities_.end()||found->second.generation!=reference.generation||
       count>found->second.attributes.size())return false;
    auto& attributes=found->second.attributes;
    bool changed=false;
    for(std::size_t n=0;n<count;++n) {
        if(attributes[n]<values[n]){attributes[n]=values[n];changed=true;}
    }
    // Observer code may remove/reuse the entity. Do not touch its storage after
    // this single notification; observers see all prefix words already merged.
    if(changed)mark_spatial_change();
    return true;
}
VmMemory World::vm_memory() {
    VmMemory memory;
    memory.resolve_live_entity=[this](std::int16_t id)->std::optional<WorldEntityReference>{
        const auto* entity=find(id);return entity?std::optional<WorldEntityReference>{{id,entity->generation}}:std::nullopt;
    };
    memory.read=[this](const VmFrame& frame,std::uint8_t scope,std::int16_t index) {
        return read(frame,scope,index);
    };
    memory.write=[this](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value) {
        return write(frame,scope,index,value);
    };
    memory.expression_levels=[this](const VmFrame& frame)
        -> std::optional<std::array<std::int32_t,3>> {
        const auto* caller=frame_entity(frame,VmEntityRole::caller);
        const auto* callee=frame_entity(frame,VmEntityRole::callee);
        if(!caller || !callee || !caller->position || !callee->position) return {};
        const auto offset=callee->definition_words.find(56);
        if(offset==callee->definition_words.end()) return {};
        // OBJD LevelOffset is unsigned16, unlike the signed word storage.
        return std::array<std::int32_t,3>{caller->position->level,callee->position->level,
            static_cast<std::uint16_t>(offset->second)};
    };
    memory.dynamic_sprite_flag=[this](const VmFrame& frame,std::uint16_t flag) {
        const auto* entity=frame_entity(frame,VmEntityRole::stack_object);if(!entity)return std::optional<bool>{};
        return std::optional<bool>{(entity->dynamic_sprite_flags&(std::uint64_t{1}<<(flag&63U)))!=0};
    };
    memory.set_dynamic_sprite_flag=[this](const VmFrame& frame,std::uint16_t flag,bool enabled) {
        auto* entity=frame_entity_mutable(frame,VmEntityRole::stack_object);if(!entity)return false;
        const auto mask=std::uint64_t{1}<<(flag&63U);if(enabled)entity->dynamic_sprite_flags|=mask;else entity->dynamic_sprite_flags&=~mask;return true;
    };
    return memory;
}
} // namespace odh
