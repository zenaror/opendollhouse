#pragma once

#include "odh/vm.hpp"
#include "odh/entry_points.hpp"
#include <array>

namespace odh {
enum class EntityKind { object, avatar };
enum class AttributePolicy { fixed, ts1_bounded };
struct EntityType {std::uint32_t guid;std::optional<std::uint32_t> master_guid;};
struct EntityLayout {
    std::size_t attributes = 0;
    std::size_t person_data = 0;
    std::size_t slots = 0;
    std::size_t motives = 0;
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
struct SavedObjectBanks {
    std::array<std::int16_t,68> object_data;
    std::array<std::int16_t,5> extras;
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
    std::vector<std::int16_t> motives;
    std::map<std::int16_t, std::int16_t> definition_words;
    std::optional<std::vector<EntryPoint>> entry_points; // Unset differs from an explicitly empty table.
    // Original owned save snapshot; runtime writes never change these banks.
    std::optional<SavedObjectBanks> saved_object_banks;
    // Save baseline while uncontained; ordinary entities keep the existing -1.
    std::int16_t unattached_slot=-1;
    // Runtime RoomInfo collection state. Simitone starts entities static and
    // changes routed avatars dynamically while walking; this is not save data.
    bool static_footprint=true;
    // New VM avatars start alive. Death/leave controllers may update this;
    // absent state remains possible for imported profiles that cannot prove it.
    std::optional<std::int32_t> kill_timeout;
    // VMAvatar.RadianDirection is continuous during turns; EntityPosition only
    // exposes the derived eight-notch direction used by saved-object adapters.
    float radian_direction = 0.0f;
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
    // ts1_bounded reads absent attribute words as zero and grows on write,
    // within bank_words and the nonnegative signed VM index range (32768 words).
    // This bounded profile does not implement the reference's full ushort range.
    explicit World(WorldLimits limits = {}, AttributePolicy attributes = AttributePolicy::fixed)
        : limits_(limits), attribute_policy_(attributes) {}
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;
    bool create(std::int16_t id, EntityKind kind, EntityLayout layout = {});
    // Import a new typed entity atomically. All banks are copied after validation;
    // motives retain their signed values without the runtime setter's clamp.
    // Raw ObjectData/extras consume 73 words in addition to 64 ordinary runtime
    // ObjectData words; derived indices 2/3/11/67 retain their existing getters.
    // Slots start empty for a subsequent validated containment import.
    // Allocation exceptions propagate without publishing state or a generation.
    bool create_saved_entity(std::int16_t id, EntityKind kind, std::uint32_t guid,
        const std::vector<std::int16_t>& attributes,
        const std::vector<std::int16_t>& person_data,
        const std::vector<std::int16_t>& motives,
        const std::array<std::int16_t,68>& object_data,
        const std::array<std::int16_t,5>& extras, std::size_t slots,
        std::optional<std::uint32_t> master_guid={});
    bool remove(std::int16_t id);
    const Entity* find(std::int16_t id) const;
    // Live positive-ID enumeration, including entities with unknown definitions.
    std::optional<std::int16_t> next_entity(std::int16_t current,std::optional<EntityKind> kind={}) const;
    // Live exact definition GUID query, including avatars; no master alias or
    // wraparound. The signed cursor need not identify an existing entity.
    std::optional<std::int16_t> next_entity_of_guid(std::int16_t current,std::uint32_t guid) const;
    bool define_type(std::int16_t id,std::uint32_t guid,std::optional<std::uint32_t> master_guid={});
    // Bind an immutable owned table to a typed entity; identical repeats succeed.
    // Each condition/action pair consumes two words. Empty tables are valid.
    // Validation/allocation precedes publication; failures leave state unchanged.
    bool define_entry_points(std::int16_t id,const std::vector<EntryPoint>& values);
    bool declare_object_word(std::int16_t id, std::int16_t index, std::int16_t value);
    bool set_person_data(std::int16_t id, std::int16_t index, std::int16_t value);
    // Atomically replace an avatar's bank with an owned copy (empty is valid).
    // Validation returns false without mutation; allocation exceptions propagate
    // without changing entity state or word accounting.
    bool replace_person_data(std::int16_t id, const std::vector<std::int16_t>& values);
    bool set_motive(std::int16_t id, std::int16_t index, std::int16_t value);
    bool declare_definition_word(std::int16_t id, std::int16_t index, std::int16_t value);
    bool set_static_footprint(std::int16_t id, bool is_static);
    bool set_avatar_kill_timeout(std::int16_t id, std::int32_t value);
    bool set_radian_direction(std::int16_t id, float radians);
    bool set_position(std::int16_t id, EntityPosition);
    bool attach(std::int16_t container, std::size_t slot, std::int16_t occupant);
    bool place_in_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                       std::optional<std::uint8_t> direction={});
    // Validate and allocate the positioned descendant list before detaching.
    // Preserves child orientations and leaves invalid placements unchanged.
    bool place_on_floor(std::int16_t id,EntityPosition);
    bool detach(std::int16_t container, std::size_t slot);
    std::optional<std::int16_t> read(const VmFrame&, std::uint8_t scope, std::int16_t index) const;
    // Attribute growth validates budget and allocates before publication.
    // Allocation exceptions propagate without changing state or word accounting.
    bool write(const VmFrame&, std::uint8_t scope, std::int16_t index, std::int16_t value);
    // World must outlive bound VMs. Its address is stable (copy/move disabled).
    VmMemory vm_memory();
    std::size_t word_count() const { return words_; }
private:
    std::vector<std::int16_t> position_tree(std::int16_t) const;
    void apply_position(const std::vector<std::int16_t>&,EntityPosition);
    std::map<std::int16_t, Entity> entities_;
    WorldLimits limits_;
    AttributePolicy attribute_policy_;
    std::size_t words_ = 0;
    std::uint64_t generation_sequence_ = 0;
};
} // namespace odh
