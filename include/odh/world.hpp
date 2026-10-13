#pragma once

#include "odh/vm.hpp"

namespace odh {
enum class EntityKind { object, avatar };
struct EntityType {std::uint32_t guid;std::optional<std::uint32_t> master_guid;};
struct EntityLayout {
    std::size_t attributes = 0;
    std::size_t person_data = 0;
    std::size_t slots = 0;
};
struct Attachment { std::int16_t container; std::size_t slot; bool positioned=false; };
struct EntityPosition {
    std::int32_t x,y; // Adapter uses 1/16 tile, not pixels.
    std::int16_t level;
    std::uint8_t direction; // Eight notches, north=0, clockwise.
    bool operator==(const EntityPosition& p) const {
        return x==p.x && y==p.y && level==p.level && direction==p.direction;
    }
};
struct Entity {
    std::int16_t id;
    EntityKind kind;
    std::vector<std::int16_t> attributes, person_data, slots;
    std::map<std::int16_t, std::int16_t> object_words;
    std::optional<Attachment> attachment;
    std::optional<EntityPosition> position;
    std::uint64_t generation;
    std::optional<EntityType> definition; // Explicit known single part or known master identity.
};
struct WorldLimits {
    std::size_t entities = 4096;
    std::size_t words = 65536;
    std::size_t bank_words = 4096;
};
// Minimal entity state. Layouts/initial values come from explicit construction;
// this does not load a lot, parse SLOT/OBJD, or initialize TS1 objects.
class World {
public:
    explicit World(WorldLimits limits = {}) : limits_(limits) {}
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;
    bool create(std::int16_t id, EntityKind kind, EntityLayout layout = {});
    bool remove(std::int16_t id);
    const Entity* find(std::int16_t id) const;
    bool define_type(std::int16_t id,std::uint32_t guid,std::optional<std::uint32_t> master_guid={});
    bool declare_object_word(std::int16_t id, std::int16_t index, std::int16_t value);
    bool set_person_data(std::int16_t id, std::int16_t index, std::int16_t value);
    bool set_position(std::int16_t id, EntityPosition);
    bool attach(std::int16_t container, std::size_t slot, std::int16_t occupant);
    bool place_in_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                       std::optional<std::uint8_t> direction={});
    // Validate and allocate the positioned descendant list before detaching.
    // Preserves child orientations and leaves invalid placements unchanged.
    bool place_on_floor(std::int16_t id,EntityPosition);
    bool detach(std::int16_t container, std::size_t slot);
    std::optional<std::int16_t> read(const VmFrame&, std::uint8_t scope, std::int16_t index) const;
    bool write(const VmFrame&, std::uint8_t scope, std::int16_t index, std::int16_t value);
    // World must outlive bound VMs. Its address is stable (copy/move disabled).
    VmMemory vm_memory();
    std::size_t word_count() const { return words_; }
private:
    std::vector<std::int16_t> position_tree(std::int16_t) const;
    void apply_position(const std::vector<std::int16_t>&,EntityPosition);
    std::map<std::int16_t, Entity> entities_;
    WorldLimits limits_;
    std::size_t words_ = 0;
    std::uint64_t generation_sequence_ = 0;
};
} // namespace odh
