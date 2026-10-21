#include "odh/world.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace odh {
namespace {
bool valid_index(std::int16_t index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}
bool derived(std::int16_t index) { return index == 2 || index == 3 || index == 11 || index == 67; }
bool valid_position(EntityPosition position) {
    return position.x>=0 && position.y>=0 && position.x<=32767 && position.y<=32767 &&
           position.level>=1 && position.direction<=7;
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
std::int16_t scope_entity(const VmFrame& frame, std::uint8_t scope) {
    if (scope == 35) return frame.context.stack_object;
    return scope == 0 || scope == 3 || scope == 14 || scope == 18 || scope == 20
        ? frame.context.caller : frame.context.stack_object;
}
}
bool World::create(std::int16_t id, EntityKind kind, EntityLayout layout) {
    if (id <= 0 || generation_sequence_==std::numeric_limits<std::uint64_t>::max() ||
        (kind != EntityKind::object && kind != EntityKind::avatar) ||
        entities_.count(id) || entities_.size() >= limits_.entities ||
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
    entities_.emplace(id, Entity{id,kind,std::vector<std::int16_t>(layout.attributes),
        std::vector<std::int16_t>(layout.person_data),std::vector<std::int16_t>(layout.slots),{},
        std::nullopt,std::nullopt,++generation_sequence_,{},std::vector<std::int16_t>(layout.motives),
        {},{},{},std::int16_t{-1},true,std::nullopt});
    if (kind == EntityKind::avatar) entities_.at(id).kill_timeout = -1;
    words_ += count;
    return true;
}
bool World::create_saved_entity(std::int16_t id, EntityKind kind, std::uint32_t guid,
    const std::vector<std::int16_t>& attributes,
    const std::vector<std::int16_t>& person_data,
    const std::vector<std::int16_t>& motives,
    const std::array<std::int16_t,68>& object_data,
    const std::array<std::int16_t,5>& extras, std::size_t slots,
    std::optional<std::uint32_t> master_guid) {
    if (id<=0 || !guid || (master_guid && !*master_guid) ||
        generation_sequence_==std::numeric_limits<std::uint64_t>::max() ||
        (kind!=EntityKind::object && kind!=EntityKind::avatar) ||
        entities_.count(id) || entities_.size()>=limits_.entities ||
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
    Entity candidate{id,kind,attributes,person_data,std::vector<std::int16_t>(slots),{},
        {},{},generation,EntityType{guid,master_guid},motives,{},{},
        SavedObjectBanks{object_data,extras},object_data[2]==0 ? object_data[3] : std::int16_t{-1},
        true,std::nullopt};
    for (std::int16_t index=0;index<68;++index)
        if (!derived(index)) candidate.object_words.emplace(index,object_data[static_cast<std::size_t>(index)]);
    entities_.emplace(id,std::move(candidate));
    if (kind == EntityKind::avatar) entities_.at(id).kill_timeout = -1;
    generation_sequence_=generation;
    words_+=count;
    return true;
}
const Entity* World::find(std::int16_t id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : &found->second;
}
std::optional<std::int16_t> World::next_entity_of_guid(std::int16_t current,std::uint32_t guid) const {
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it)
        if(it->second.definition && it->second.definition->guid==guid)return it->first;
    return {};
}
std::optional<std::int16_t> World::next_entity(std::int16_t current,std::optional<EntityKind> kind) const {
    for(auto it=entities_.upper_bound(current);it!=entities_.end();++it)
        if(!kind || it->second.kind==*kind)return it->first;
    return {};
}
bool World::define_type(std::int16_t id,std::uint32_t guid,std::optional<std::uint32_t> master_guid) {
    const auto found=entities_.find(id);if(found==entities_.end())return false;
    auto& type=found->second.definition;
    if(type)return type->guid==guid&&type->master_guid==master_guid;
    type=EntityType{guid,master_guid};return true;
}
bool World::define_entry_points(std::int16_t id,const std::vector<EntryPoint>& values) {
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
    return true;
}
bool World::declare_object_word(std::int16_t id, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(id);
    // Current TS1 reference bank; FSO's extension query 79 is outside this profile.
    if (found == entities_.end() || index < 0 || index >= 79 || derived(index)) return false;
    auto& bank = found->second.object_words;
    const auto field = bank.find(index);
    if (field != bank.end()) { field->second = value; return true; }
    if (words_ >= limits_.words) return false;
    bank.emplace(index,value); ++words_; return true;
}
bool World::set_person_data(std::int16_t id, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(id);
    if (found == entities_.end() || found->second.kind != EntityKind::avatar ||
        !valid_index(index,found->second.person_data.size())) return false;
    found->second.person_data[static_cast<std::size_t>(index)] = value; return true;
}
bool World::replace_person_data(std::int16_t id, const std::vector<std::int16_t>& values) {
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
    return true;
}
bool World::set_motive(std::int16_t id, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(id);
    if (found == entities_.end() || found->second.kind != EntityKind::avatar ||
        !valid_index(index,found->second.motives.size())) return false;
    auto& old = found->second.motives[static_cast<std::size_t>(index)];
    // Fixed reference default: no tuning override or motive decay in this world.
    old = std::clamp(value,static_cast<std::int16_t>(-100),
                     std::max(old,static_cast<std::int16_t>(100)));
    return true;
}
bool World::declare_definition_word(std::int16_t id, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(id);
    if (found == entities_.end() || index < 0 || index > 255) return false;
    auto& bank = found->second.definition_words;
    const auto field = bank.find(index);
    if (field != bank.end()) return field->second == value;
    if (bank.size() >= limits_.bank_words || words_ >= limits_.words) return false;
    bank.emplace(index,value); ++words_; return true;
}
bool World::set_static_footprint(std::int16_t id, bool is_static) {
    const auto found=entities_.find(id);
    if(found==entities_.end()) return false;
    found->second.static_footprint=is_static;
    return true;
}
bool World::set_avatar_kill_timeout(std::int16_t id, std::int32_t value) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.kind!=EntityKind::avatar || value < -1) return false;
    found->second.kill_timeout=value;
    return true;
}
std::vector<std::int16_t> World::position_tree(std::int16_t id) const {
    std::vector<std::int16_t> descendants{id};
    for(std::size_t n=0;n<descendants.size();++n)
        for(const auto occupant:entities_.at(descendants[n]).slots)
            if(occupant && entities_.at(occupant).attachment->positioned) descendants.push_back(occupant);
    return descendants;
}
void World::apply_position(const std::vector<std::int16_t>& descendants,EntityPosition position) {
    auto& root=entities_.at(descendants[0]);
    root.position=position;
    if(root.kind==EntityKind::avatar && direction_notch(root.radian_direction)!=position.direction)
        root.radian_direction=radians_for_notch(position.direction);
    if (auto direction=root.object_words.find(1); direction!=root.object_words.end())
        direction->second=position.direction;
    for(std::size_t n=1;n<descendants.size();++n) {
        auto& child=entities_.at(descendants[n]);
        auto p=position;p.direction=child.position->direction;child.position=p;
        if(child.kind==EntityKind::avatar && direction_notch(child.radian_direction)!=p.direction)
            child.radian_direction=radians_for_notch(p.direction);
        if (auto direction=child.object_words.find(1); direction!=child.object_words.end())
            direction->second=p.direction;
    }
}
bool World::set_position(std::int16_t id,EntityPosition position) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.attachment || !valid_position(position)) return false;
    const auto descendants=position_tree(id); // Allocation happens before any mutation.
    apply_position(descendants,position);return true;
}
bool World::set_radian_direction(std::int16_t id,float radians) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.kind!=EntityKind::avatar || !std::isfinite(radians))
        return false;
    auto& avatar=found->second;
    const auto notch=direction_notch(radians);
    avatar.radian_direction=radians;
    if(avatar.position) avatar.position->direction=notch;
    if(auto direction=avatar.object_words.find(1);direction!=avatar.object_words.end())
        direction->second=notch;
    return true;
}
bool World::place_on_floor(std::int16_t id,EntityPosition position) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || !valid_position(position)) return false;
    const auto old=found->second.attachment;
    if(old) {
        const auto parent=entities_.find(old->container);
        if(parent==entities_.end() || old->slot>=parent->second.slots.size() ||
           parent->second.slots[old->slot]!=id) return false;
    }
    const auto descendants=position_tree(id); // No allocation after changing either relation end.
    if(old) {
        entities_.at(old->container).slots[old->slot]=0;
        found->second.attachment.reset();
        if (found->second.saved_object_banks) found->second.unattached_slot=-1;
    }
    apply_position(descendants,position);return true;
}
bool World::place_in_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                          std::optional<std::uint8_t> direction) {
    const auto c=entities_.find(container),o=entities_.find(occupant);
    if(c==entities_.end() || o==entities_.end() || !c->second.position || !o->second.position ||
       slot>=c->second.slots.size() || (direction&&*direction>7)) return false;
    const bool same=c->second.slots[slot]==occupant && o->second.attachment &&
                    o->second.attachment->container==container && o->second.attachment->slot==slot;
    const auto descendants=position_tree(occupant);
    if(!same && !attach(container,slot,occupant)) return false;
    auto p=*c->second.position;p.direction=direction.value_or(o->second.position->direction);
    apply_position(descendants,p);o->second.attachment->positioned=true;return true;
}
bool World::attach(std::int16_t container, std::size_t slot, std::int16_t occupant) {
    const auto c = entities_.find(container), o = entities_.find(occupant);
    if (c == entities_.end() || o == entities_.end() || container == occupant ||
        slot >= c->second.slots.size() || c->second.slots[slot] != 0 || o->second.attachment) return false;
    // Reject containment cycles before changing either side of the relation.
    const Entity* ancestor = &c->second;
    while (ancestor->attachment) {
        const auto parent = ancestor->attachment->container;
        if (parent == occupant) return false;
        ancestor = find(parent);
        if (!ancestor) return false;
    }
    c->second.slots[slot] = occupant;
    o->second.attachment = Attachment{container,slot};
    return true;
}
bool World::detach(std::int16_t container, std::size_t slot) {
    const auto c = entities_.find(container);
    if (c == entities_.end() || slot >= c->second.slots.size()) return false;
    const auto occupant = c->second.slots[slot];
    if (occupant) {
        auto& entity=entities_.at(occupant);
        entity.attachment.reset();
        if (entity.saved_object_banks) entity.unattached_slot=-1;
    }
    c->second.slots[slot] = 0; return true;
}
bool World::remove(std::int16_t id) {
    const auto found = entities_.find(id);
    if (found == entities_.end()) return false;
    auto& entity = found->second;
    if (entity.attachment) detach(entity.attachment->container,entity.attachment->slot);
    for (std::size_t slot = 0; slot < entity.slots.size(); ++slot) detach(id,slot);
    words_ -= entity.attributes.size() + entity.person_data.size() + entity.slots.size() +
              entity.object_words.size() + entity.motives.size() + entity.definition_words.size() +
              (entity.entry_points ? 2*entity.entry_points->size() : 0) +
              (entity.saved_object_banks ? 73 : 0);
    entities_.erase(found); return true;
}
std::optional<std::int16_t> World::read(const VmFrame& frame, std::uint8_t scope, std::int16_t index) const {
    if (scope != 0 && scope != 1 && scope != 3 && scope != 4 && scope != 14 && scope != 15 &&
        scope != 16 && scope != 18 && scope != 19 && scope != 20 && scope != 21 && scope != 35) return std::nullopt;
    const auto* entity = find(scope_entity(frame,scope));
    if (!entity) return std::nullopt;
    if (scope == 35) {
        if (!entity->entry_points || !valid_index(index,entity->entry_points->size())) return std::nullopt;
        const auto action=(*entity->entry_points)[static_cast<std::size_t>(index)].action;
        // Preserve the low 16 bits with a defined signed conversion.
        return static_cast<std::int16_t>(action<=32767 ? static_cast<std::int32_t>(action)
                                                     : static_cast<std::int32_t>(action)-65536);
    }
    if (scope == 21) {
        const auto field = entity->definition_words.find(index);
        return field == entity->definition_words.end() ? std::nullopt : std::optional<std::int16_t>(field->second);
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
        if (entity->kind != EntityKind::avatar || !valid_index(index,entity->person_data.size())) return std::nullopt;
        return entity->person_data[static_cast<std::size_t>(index)];
    }
    if (scope == 16 || scope == 20) {
        if (!valid_index(index,entity->slots.size())) return std::nullopt;
        return entity->slots[static_cast<std::size_t>(index)];
    }
    if (index == 11) return entity->id;
    if (index == 67) return static_cast<std::int16_t>(entity->slots.size());
    if (index == 2) return entity->attachment ? entity->attachment->container : static_cast<std::int16_t>(0);
    if (index == 3) return entity->attachment ? static_cast<std::int16_t>(entity->attachment->slot) : entity->unattached_slot;
    const auto field = entity->object_words.find(index);
    return field == entity->object_words.end() ? std::nullopt : std::optional<std::int16_t>(field->second);
}
bool World::write(const VmFrame& frame, std::uint8_t scope, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(scope_entity(frame,scope));
    if (found == entities_.end()) return false;
    auto& entity = found->second;
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
            return true;
        }
        entity.attributes[static_cast<std::size_t>(index)] = value; return true;
    }
    if (scope == 18 || scope == 19) return set_person_data(entity.id,index,value);
    if (scope == 14 || scope == 15) return set_motive(entity.id,index,value);
    if ((scope == 3 || scope == 4) && !derived(index)) {
        const auto field = entity.object_words.find(index);
        if (field == entity.object_words.end()) return false;
        if (index == 1) {
            if (value < 0 || value > 7) return false;
            if (entity.position) entity.position->direction = static_cast<std::uint8_t>(value);
            if (entity.kind==EntityKind::avatar)
                entity.radian_direction=radians_for_notch(static_cast<std::uint8_t>(value));
        }
        field->second = value; return true;
    }
    return false;
}
VmMemory World::vm_memory() {
    return {[this](const VmFrame& frame,std::uint8_t scope,std::int16_t index) { return read(frame,scope,index); },
            [this](const VmFrame& frame,std::uint8_t scope,std::int16_t index,std::int16_t value) {
                return write(frame,scope,index,value);
            }};
}
} // namespace odh
