#include "odh/world.hpp"
#include <limits>

namespace odh {
namespace {
bool valid_index(std::int16_t index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}
bool derived(std::int16_t index) { return index == 2 || index == 3 || index == 11 || index == 67; }
std::int16_t scope_entity(const VmFrame& frame, std::uint8_t scope) {
    return scope == 0 || scope == 3 || scope == 18 || scope == 20
        ? frame.context.caller : frame.context.stack_object;
}
}
bool World::create(std::int16_t id, EntityKind kind, EntityLayout layout) {
    if (id <= 0 || generation_sequence_==std::numeric_limits<std::uint64_t>::max() ||
        (kind != EntityKind::object && kind != EntityKind::avatar) ||
        entities_.count(id) || entities_.size() >= limits_.entities ||
        layout.attributes > limits_.bank_words || layout.person_data > limits_.bank_words ||
        layout.slots > limits_.bank_words || layout.attributes > 32768 ||
        layout.person_data > 32768 || layout.slots > 32767 ||
        (kind != EntityKind::avatar && layout.person_data != 0)) return false;
    // Subtract before summing, so even caller-provided limits cannot overflow.
    auto available = limits_.words - words_;
    for (const auto count : {layout.attributes, layout.person_data, layout.slots}) {
        if (count > available) return false;
        available -= count;
    }
    const auto count = layout.attributes + layout.person_data + layout.slots;
    entities_.emplace(id, Entity{id,kind,std::vector<std::int16_t>(layout.attributes),
        std::vector<std::int16_t>(layout.person_data),std::vector<std::int16_t>(layout.slots),{},
        std::nullopt,std::nullopt,++generation_sequence_});
    words_ += count;
    return true;
}
const Entity* World::find(std::int16_t id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : &found->second;
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
bool World::set_position(std::int16_t id,EntityPosition position) {
    const auto found=entities_.find(id);
    if(found==entities_.end() || found->second.attachment || position.x<0 || position.y<0 ||
       position.x>32767 || position.y>32767 || position.level<1 || position.direction>7) return false;
    found->second.position=position;return true;
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
    if (occupant) entities_.at(occupant).attachment.reset();
    c->second.slots[slot] = 0; return true;
}
bool World::remove(std::int16_t id) {
    const auto found = entities_.find(id);
    if (found == entities_.end()) return false;
    auto& entity = found->second;
    if (entity.attachment) detach(entity.attachment->container,entity.attachment->slot);
    for (std::size_t slot = 0; slot < entity.slots.size(); ++slot) detach(id,slot);
    words_ -= entity.attributes.size() + entity.person_data.size() + entity.slots.size() + entity.object_words.size();
    entities_.erase(found); return true;
}
std::optional<std::int16_t> World::read(const VmFrame& frame, std::uint8_t scope, std::int16_t index) const {
    if (scope != 0 && scope != 1 && scope != 3 && scope != 4 && scope != 16 &&
        scope != 18 && scope != 19 && scope != 20) return std::nullopt;
    const auto* entity = find(scope_entity(frame,scope));
    if (!entity) return std::nullopt;
    if (scope == 0 || scope == 1) {
        if (!valid_index(index,entity->attributes.size())) return std::nullopt;
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
    if (index == 3) return entity->attachment ? static_cast<std::int16_t>(entity->attachment->slot) : static_cast<std::int16_t>(-1);
    const auto field = entity->object_words.find(index);
    return field == entity->object_words.end() ? std::nullopt : std::optional<std::int16_t>(field->second);
}
bool World::write(const VmFrame& frame, std::uint8_t scope, std::int16_t index, std::int16_t value) {
    const auto found = entities_.find(scope_entity(frame,scope));
    if (found == entities_.end()) return false;
    auto& entity = found->second;
    if (scope == 0 || scope == 1) {
        if (!valid_index(index,entity.attributes.size())) return false;
        entity.attributes[static_cast<std::size_t>(index)] = value; return true;
    }
    if (scope == 18 || scope == 19) return set_person_data(entity.id,index,value);
    if ((scope == 3 || scope == 4) && !derived(index)) {
        const auto field = entity.object_words.find(index);
        if (field == entity.object_words.end()) return false;
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
