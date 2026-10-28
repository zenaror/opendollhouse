#pragma once

#include "odh/vm.hpp"
#include "odh/entry_points.hpp"
#include <array>
#include <string>

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
struct MotiveChangeState {
    std::int16_t per_hour_change=0;
    std::int16_t maximum=32767;
    double fractional=0.0;
    // Runtime-only ordering bit. Like Simitone, it is reset on session restore.
    bool ticked=false;
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
    // UI-only ghost objects can be addressed directly (for example as a
    // temporary Go Here target) but must not appear in VM object enumeration.
    bool ui_ghost=false;
    // New VM avatars start alive. Death/leave controllers may update this;
    // absent state remains possible for imported profiles that cannot prove it.
    std::optional<std::int32_t> kill_timeout;
    // VMAvatar.RadianDirection is continuous during turns; EntityPosition only
    // exposes the derived eight-notch direction used by saved-object adapters.
    float radian_direction = 0.0f;
    // Present only for avatars with a complete 16-word motive bank. This is
    // runtime state, separate from the imported saved motive values.
    std::vector<MotiveChangeState> motive_changes;
    // Runtime avatar accessories restored by OBJM and changed by Simantics.
    // Names are flat `.apr` asset identifiers, never paths.
    std::vector<std::string> bound_appearances;
    // Runtime VM state: dynamic sprite flags indexed by the game's ushort flag ID.
    std::uint64_t dynamic_sprite_flags=0;
};
struct WorldLimits {
    std::size_t entities = 4096;
    std::size_t words = 65536;
    std::size_t bank_words = 4096;
    std::size_t bound_appearance_names = 16;
    std::size_t bound_appearance_name_bytes = 4096;
    // Launch policy derived from the immutable lot retirement plan, not wire
    // state. At most 4096 positive, unique IDs; World canonicalizes their order.
    // Callers must supply the same policy to every candidate and restore host.
    std::vector<std::int16_t> reserved_entity_ids{};
};
// Owned values only; no VM callbacks, resource pointers, queues or clocks.
struct WorldSnapshot {
    std::vector<Entity> entities;
    AttributePolicy attribute_policy = AttributePolicy::fixed;
    std::size_t word_count = 0;
    std::uint64_t generation_sequence = 0;
    std::uint64_t spatial_revision = 0;
};
// Minimal entity state. Layouts/initial values come from explicit construction;
// this does not load a lot, parse SLOT/OBJD, or initialize TS1 objects.
class World {
public:
    // ts1_bounded reads absent attribute words as zero and grows on write,
    // within bank_words and the nonnegative signed VM index range (32768 words).
    // This bounded profile does not implement the reference's full ushort range.
    // Invalid reservation policy throws std::invalid_argument before use.
    explicit World(WorldLimits limits = {}, AttributePolicy attributes = AttributePolicy::fixed);
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = delete;
    World& operator=(World&&) = delete;
    // Reservations are not entities, never enumerate, and survive snapshot
    // publication/removal. Snapshot codecs intentionally do not serialize them.
    // Atomically unions launch-derived exclusions after a full-session restore,
    // preserving this World address, entity pointers, callbacks and counters.
    // Positive, unique input IDs and the union are bounded to 4096. Overlap
    // with existing reservations is allowed; any matching live entity rejects.
    // Invalid input returns false; allocation exceptions precede publication.
    // Existing reservations can never be cleared by this method.
    bool install_reserved_entity_ids(std::vector<std::int16_t> ids);
    bool entity_id_available(std::int16_t id) const;
    const std::vector<std::int16_t>& reserved_entity_ids() const { return limits_.reserved_entity_ids; }
    WorldSnapshot snapshot() const;
    // Fresh empty destination only (no entities or prior generation/spatial
    // history), using its capacity limits. Invalid values return false;
    // allocation exceptions propagate. Both leave World unchanged.
    // Restores entity state, policy and counters; not a full simulation save.
    bool restore_snapshot(const WorldSnapshot&);
    // Atomically publishes a validated snapshot into this stable World object.
    // Existing Entity pointers are invalidated; VM memory callbacks bound to
    // this World remain valid and observe the published state. Invalid values
    // or regressing generation/spatial history return false; existing entity
    // identities must be preserved and new IDs must use fresh generations.
    // Allocation exceptions propagate without changing World.
    bool publish_snapshot(const WorldSnapshot&);
    bool create(std::int16_t id, EntityKind kind, EntityLayout layout = {});
    // Atomically publishes an ordinary object with zero-filled runtime banks,
    // a known GUID and an owned entry-point table. Resource ownership and BHAV
    // execution remain the caller's responsibility. Invalid requests leave
    // the World unchanged; allocation exceptions propagate without publishing.
    bool create_runtime_object(std::int16_t id, std::uint32_t guid,
        EntityLayout layout, const std::vector<EntryPoint>& entry_points);
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
    // Remove one entity with ordinary detach semantics, and permanently reserve
    // its numeric ID. Validate capacity and allocate reservation storage BEFORE
    // removal. False/allocation failure leaves World unchanged. Not recursive;
    // host still owns execution cleanup and creation-origin retirement.
    bool retire(std::int16_t id);
    const Entity* find(std::int16_t id) const;
    // Live positive-ID enumeration, including entities with unknown definitions.
    std::optional<std::int16_t> next_entity(std::int16_t current,std::optional<EntityKind> kind={}) const;
    // TS1 SetToNext category search (mode 6): next live entity whose saved
    // ObjectData[59] matches the requested category. The cursor does not wrap.
    std::optional<std::int16_t> next_entity_by_category(std::int16_t current,
        std::int16_t category) const;
    // TS1 SetToNext FamilyMember (mode 12): enumerate avatars sharing PersonData[61],
    // ascending after the cursor and wrapping to the first matching avatar.
    std::optional<std::int16_t> next_family_member(std::int16_t current) const;
    // Live exact definition GUID query, including avatars; no master alias or
    // wraparound. The signed cursor need not identify an existing entity.
    std::optional<std::int16_t> next_entity_of_guid(std::int16_t current,std::uint32_t guid) const;
    // Live ObjectOnSameTile query in ascending ID order, wrapping to the first
    // entity on the tile after the cursor reaches the end. Supports positive lot coordinates,
    // levels 1-2; callers must reject outside-lot positions before dispatch.
    std::optional<std::int16_t> next_entity_on_same_tile(std::int16_t current) const;
    // TS1 SetToNext mode 9: inspect adjacent tiles in north/east/south/west
    // order after the current cursor direction, anchored at the selected local.
    std::optional<std::int16_t> next_entity_adjacent_to_local(std::int16_t anchor,
                                                               std::int16_t current) const;
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
    bool add_bound_appearance(std::int16_t avatar,const std::string& name);
    bool remove_bound_appearance(std::int16_t avatar,const std::string& name);
    // TS1 continuous SetMotiveChange state belongs to the Caller avatar.
    // The setter returns true when the request is valid, including the first
    // pre-tick request that Simitone deliberately ignores.
    bool set_motive_change(std::int16_t id,std::int16_t motive,
                           std::int16_t per_hour_change,std::int16_t maximum);
    bool clear_motive_changes(std::int16_t id);
    bool tick_motive_changes();
    bool has_motive_change(std::int16_t id,std::int16_t motive) const;
    bool declare_definition_word(std::int16_t id, std::int16_t index, std::int16_t value);
    bool set_static_footprint(std::int16_t id, bool is_static);
    bool set_ui_ghost(std::int16_t id, bool is_ghost);
    std::optional<bool> dynamic_sprite_flag(std::int16_t id,std::uint16_t flag) const;
    bool set_dynamic_sprite_flag(std::int16_t id,std::uint16_t flag,bool enabled);
    bool set_avatar_kill_timeout(std::int16_t id, std::int32_t value);
    bool set_radian_direction(std::int16_t id, float radians);
    bool set_position(std::int16_t id, EntityPosition);
    bool attach(std::int16_t container, std::size_t slot, std::int16_t occupant);
    bool place_in_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                       std::optional<std::uint8_t> direction={});
    // Move an existing entity from its old container into an empty slot.
    // Used by TS1 Grab; rejects occupied slots and containment cycles atomically.
    bool move_to_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                      std::optional<std::uint8_t> direction={});
    // Validate and allocate the positioned descendant list before detaching.
    // Preserves child orientations and leaves invalid placements unchanged.
    bool place_on_floor(std::int16_t id,EntityPosition);
    bool detach(std::int16_t container, std::size_t slot);
    // ObjectData26 reads current containment like2; its raw writable shadow and
    // immutable imported bank remain serialized (pinned GetValue/SetValue).
    std::optional<std::int16_t> read(const VmFrame&, std::uint8_t scope, std::int16_t index) const;
    // Attribute growth validates budget and allocates before publication.
    // Allocation exceptions propagate without changing state or word accounting.
    bool write(const VmFrame&, std::uint8_t scope, std::int16_t index, std::int16_t value);
    // World must outlive bound VMs. Its address is stable (copy/move disabled).
    VmMemory vm_memory();
    std::size_t word_count() const { return words_; }
    // Changes whenever entity state relevant to live room/route obstacles changes.
    std::uint64_t spatial_revision() const noexcept { return spatial_revision_; }
private:
    std::vector<std::int16_t> position_tree(std::int16_t) const;
    void apply_position(const std::vector<std::int16_t>&,EntityPosition);
    void mark_spatial_change() noexcept;
    std::map<std::int16_t, Entity> entities_;
    WorldLimits limits_;
    AttributePolicy attribute_policy_;
    std::size_t words_ = 0;
    std::uint64_t generation_sequence_ = 0;
    std::uint64_t spatial_revision_ = 0;
};
} // namespace odh
