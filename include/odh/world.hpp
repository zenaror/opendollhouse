#pragma once

#include "odh/vm.hpp"
#include "odh/entry_points.hpp"
#include <array>
#include <string>
#include <utility>
#include <tuple>
#include <memory>
#include <set>

namespace odh {
enum class EntityKind { object, avatar };
enum class FootprintClassChangeSource { obstacle_static,flags_assignment };
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
struct EntityLockoutCountState {
    bool thread_present=false;
    // GetValue(25) logically mutates a future timestamp back to zero. Unknown
    // checkpoint history is distinct from the proven source default zero.
    mutable std::optional<std::uint32_t> timestamp;
};
enum class AvatarOutfitKind : std::uint8_t { literal, identifier, none };
struct AvatarOutfitReference {
    AvatarOutfitKind kind=AvatarOutfitKind::literal;
    std::string literal;
    std::array<std::string,6> hands{};
    std::uint64_t identifier=0;
    bool operator==(const AvatarOutfitReference& b) const {
        return kind==b.kind&&literal==b.literal&&hands==b.hands&&identifier==b.identifier;
    }
    bool operator!=(const AvatarOutfitReference& b) const {return !(*this==b);}
};
struct AvatarAppearanceState {
    std::optional<AvatarOutfitReference> body,default_daywear;
    // Actual visual resource set is distinct from the source's exact raw keys.
    std::optional<std::vector<std::string>> accessory_assets;
    bool operator==(const AvatarAppearanceState& b) const {
        return body==b.body&&default_daywear==b.default_daywear&&accessory_assets==b.accessory_assets;
    }
    bool operator!=(const AvatarAppearanceState& b) const {return !(*this==b);}
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
    // Exact source keys: primitive .apr names or animation raw property values.
    std::vector<std::string> bound_appearances;
    // Runtime VM state: dynamic sprite flags indexed by the game's ushort flag ID.
    std::uint64_t dynamic_sprite_flags=0;
    std::optional<EntityLockoutCountState> lockout_count;
    std::optional<AvatarAppearanceState> avatar_appearance;
    std::optional<std::string> name_override = std::nullopt; // Absent preserves original/imported resource name.
    bool dead=false; // Marked at source end-of-frame Delete, before index removal.
    // Runtime derived high-word lookup selectors. Unknown bits are not false.
    // Not an original low-word lookup index and not serialized by ODHS codecs.
    std::uint16_t lookup_selectors=0,known_lookup_selectors=0;
    // Native instance policy for suppressing referenced-object reset. This is
    // not ObjectData or the temporary InReset guard. Unknown until import or a
    // proven native writer establishes it. Older archives leave it unknown.
    std::optional<bool> suppress_referenced_reset;
    // Native instance +114 bit6 gates the Person.Reset position fallback.
    // Its wider meaning is not inferred from ghost/dead or ObjectData flags.
    std::optional<bool> skip_native_recovery_position;
    // Native backing-vector slot zero: separate from physical slots (raw1+).
    // Unknown in old archives; {0,0} is authenticated empty storage.
    std::optional<WorldEntityReference> native_slot0;
    // Explicit native scalar sentinel (-16,-16,+110 level). Absence means
    // unknown, not floor1. Heading remains the current raw ObjectData1.
    // In-world bodies use position directly and leave this field absent.
    std::optional<std::int16_t> native_out_of_world_level;
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
    std::size_t room_registration_events=4096; // Runtime-only bounded history.
};
struct WorldRoomRegistrationEvent {
    std::uint64_t serial=0,generation=0;
    std::int16_t entity=0;
    std::optional<EntityPosition> before,after;
    bool all_rooms=false;
};
// Owned values only; no VM callbacks, resource pointers or queues.
struct WorldSnapshot {
    std::vector<Entity> entities;
    AttributePolicy attribute_policy = AttributePolicy::fixed;
    std::size_t word_count = 0;
    std::uint64_t generation_sequence = 0;
    std::uint64_t spatial_revision = 0;
    // Absence preserves the legacy raw ObjectData25 adapter. Presence opts in
    // to source scheduler-relative reads and requires explicit Thread proof.
    std::optional<std::uint32_t> lockout_count_clock;
    std::vector<Entity> retained_entities; // Dead bodies, canonical (id,generation), never live/query members.
};
class World;
// Owned, value-only preparation for source PlaceInSlot(cleanOld=true). The
// World must outlive the plan. Resource/animation/Placement preflight belongs
// to the primitive; no callback or allocation runs during World publication.
class PreparedSlotMove {
public:
    PreparedSlotMove(const PreparedSlotMove&)=default;
    PreparedSlotMove(PreparedSlotMove&&)=default;
    PreparedSlotMove& operator=(const PreparedSlotMove&)=default;
    PreparedSlotMove& operator=(PreparedSlotMove&&)=default;
    std::int16_t container() const noexcept {return container_;}
    std::int16_t occupant() const noexcept {return occupant_;}
    std::size_t slot() const noexcept {return slot_;}
    std::uint64_t container_generation() const noexcept {return container_generation_;}
    std::uint64_t occupant_generation() const noexcept {return occupant_generation_;}
    std::uint64_t source_generation() const noexcept {return source_generation_;}
    const std::optional<Attachment>& source_attachment() const noexcept {return source_;}
    bool already_in_slot() const noexcept {return already_;}
private:
    friend class World;
    PreparedSlotMove()=default;
    struct PositionUpdate {
        std::int16_t id;std::uint64_t generation;std::optional<EntityPosition> position;
        std::optional<std::int16_t> room;
        // Shared staging preserves PreparedSlotMove's existing copy semantics.
        // The winning commit extracts this preallocated map node exactly once.
        std::shared_ptr<std::map<std::int16_t,std::int16_t>> room_node;
    };
    const World* world_=nullptr;
    std::uint64_t spatial_revision_=0,generation_sequence_=0,publication_epoch_=0,room_capability_revision_=0;
    std::int16_t container_=0,occupant_=0;
    std::size_t slot_=0;
    std::vector<std::int16_t> expanded_slots_;
    std::uint64_t container_generation_=0,occupant_generation_=0,source_generation_=0;
    std::optional<Attachment> source_;
    std::vector<PositionUpdate> updates_;
    std::map<std::int16_t,std::int16_t> native_parent_nodes_;
    bool native_parent_known_=false;
    bool already_=false,consumed_=false;
};
// Runtime-only authority for one original OBJM post-load pass. It is never
// serialized; closing/moving away the lease cannot leave an active cursor.
enum class OriginalLoadLinkStatus { member, end, unavailable };
struct OriginalLoadLink {
    OriginalLoadLinkStatus status=OriginalLoadLinkStatus::unavailable;
    WorldEntityReference reference{};
};
class OriginalLoadTraversal {
public:
    OriginalLoadTraversal(const OriginalLoadTraversal&)=delete;
    OriginalLoadTraversal& operator=(const OriginalLoadTraversal&)=delete;
    OriginalLoadTraversal(OriginalLoadTraversal&&)=default;
    OriginalLoadTraversal& operator=(OriginalLoadTraversal&& other) noexcept {
        if(this!=&other){close();active_=std::move(other.active_);}return *this;
    }
    ~OriginalLoadTraversal(){close();}
    void close() noexcept {if(active_)*active_=false;active_.reset();}
private:
    explicit OriginalLoadTraversal(std::shared_ptr<bool> active):active_(std::move(active)){}
    std::shared_ptr<bool> active_;
    friend class World;
};
// One notifier's borrowed PERSON storage identity. Copies observe the same live
// vector end; they never acquire a replacement import lease or own its lifetime.
class OriginalLoadPersonCursor {
public:
    OriginalLoadPersonCursor()=default;
private:
    const World* world_=nullptr;
    std::weak_ptr<bool> active_;
    const WorldEntityReference* storage_=nullptr;
    friend class World;
};
// Minimal entity state. Layouts/initial values come from explicit construction;
// this does not load a lot, parse SLOT/OBJD, or initialize TS1 objects.
// Internal atomic personal-state publication for authenticated projections.
// Not a stable mod API. Spatial ownership, identities and every other Entity
// field remain owned by the live World and cannot be replaced by this record.
struct AvatarPersonalState {
    std::vector<std::int16_t> person_data,motives;
    std::optional<AvatarAppearanceState> appearance;
};
struct AvatarPersonalStateChange {
    WorldEntityReference reference;
    AvatarPersonalState expected,replacement;
    std::optional<EntityPosition> expected_position;
    std::optional<Attachment> expected_attachment;
};

class World {
public:
    using RoomResolver=std::function<std::optional<std::uint16_t>(const EntityPosition&)>;
    // Runtime-only geometry capability. Binding does not rewrite saved fields
    // or spatial revisions. Once bound, actual non-ghost position publication
    // stages Room29=room-1 atomically for all affected positioned descendants.
    // Unknown geometry or callback mutation/reentry rejects before publication.
    // No binding retains bare World's geometry-independent construction API.
    bool set_room_resolver(RoomResolver);
    // Pure TS1 Person-suit resolution for direct CurrentOutfit writes. Absence
    // means unsupported resource evidence; known source-null uses Kind::none.
    using AvatarOutfitResolver=std::function<std::optional<AvatarOutfitReference>(const World&,std::int16_t,std::uint16_t)>;
    bool set_avatar_outfit_resolver(AvatarOutfitResolver);
    // Resource providers may read World; mutations and stateful reads are rejected.
    bool evaluate_appearance_lookup(const std::function<void()>&) const;
    bool set_avatar_outfit(std::int16_t,const AvatarOutfitReference&,bool update_default=false,
        std::optional<std::int16_t> current_outfit={});

    struct CurrentValueReadResult {bool handled=false;std::optional<std::int16_t> value;};
    using CurrentValueReadResolver=std::function<CurrentValueReadResult(const World&,WorldEntityReference)>;
    using CurrentValueWriteObserver=std::function<bool(const World&,WorldEntityReference,std::int16_t before,
        std::int16_t after,const std::function<bool()>& store)>;
    // One runtime owner. Not serialized. Unhandled reads preserve raw legacy
    // fields; handled unknown reads remain unknown. Live object OD41 writes,
    // including equal values, invoke the observer before storage. Its fixed
    // store capability is synchronous and single-use; no World mutation/reentry
    // is permitted. declare_object_word, restore and retained bodies bypass it.
    bool set_current_value_read_resolver(CurrentValueReadResolver);
    bool set_current_value_write_observer(CurrentValueWriteObserver);
    using DefinitionWordResolver = std::function<std::optional<std::int16_t>(const Entity&,std::int16_t)>;
    // Runtime-only exact-resource fallback for missing scope21 fields. Explicit
    // owned definition_words take precedence. Never serialized or copied into
    // snapshots; the host must bind a resolver with pinned resource lifetimes
    // for every active World, covering saved and newly created entities alike.
    void set_definition_word_resolver(DefinitionWordResolver resolver);
    // ts1_bounded reads absent attribute words as zero and grows on write,
    // within bank_words and the nonnegative signed VM index range (32768 words).
    // This bounded profile does not implement the reference's full ushort range.
    // Invalid reservation policy throws std::invalid_argument before use.
    explicit World(WorldLimits limits = {}, AttributePolicy attributes = AttributePolicy::fixed);
    // Snapshot values plus existing pure read providers; no publication,
    // deletion, routing or observer authority is copied into a check world.
    std::unique_ptr<World> clone_for_check() const;
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
    // Bind ONLY after authenticating the complete original serialized source
    // order, before bootstrap callbacks. All current entities must be original
    // saved entities, live and present exactly once with their exact generations.
    // The host supplies source order; no ID sort or position inference occurs.
    // One scoped owner, bounded to limit live plus retained link nodes,
    // including later creations; body collection releases retained capacity.
    // Ordinary create/runtime constructors prepend; create_saved_entity appends.
    // Retaining removal owns the exact body and its original outgoing link
    // until collection. Hard-removed/collected cursors are unavailable; reused
    // IDs never substitute for the original body or its generation-bound NEXT.
    // Public snapshot publication invalidates the authority and permanently
    // declines rebinding on that World; ordinary ODHS restore has no authority.
    // Validation/allocation failure leaves World and any existing lease intact.
    // An explicit non0x37 source version proves the initial native policy zero;
    // omitted/0x37 versions do not establish an unknown serialized policy.
    // Rebinding a closed lease never reinitializes or proves instance policy.
    std::optional<OriginalLoadTraversal> bind_original_load_order(
        const std::vector<WorldEntityReference>& serialized_order,std::size_t limit=4096,
        std::optional<std::uint32_t> source_version={});
    OriginalLoadLink original_load_head(const OriginalLoadTraversal&) const noexcept;
    OriginalLoadLink original_load_next(const OriginalLoadTraversal&,WorldEntityReference) const noexcept;
    // Native PERSON registration vector, separate from World's linked order.
    // Storage is reserved for the complete scoped limit before effects. Each
    // query reads current length: equal means END, past end is unavailable.
    OriginalLoadLink original_load_person_at(const OriginalLoadTraversal&,std::size_t) const noexcept;
    std::optional<OriginalLoadPersonCursor> begin_original_person_notifications(const OriginalLoadTraversal&) const noexcept;
    OriginalLoadLink original_load_person_at(const OriginalLoadPersonCursor&,std::size_t) const noexcept;
    // Creation consumes a fresh generation; removal changes live count.
    // UI-only visibility changes also alter the source enumeration view.
    std::tuple<std::uint64_t,std::size_t,std::uint64_t> entity_identity_revision() const noexcept {
        return {generation_sequence_,entities_.size(),entity_visibility_revision_};
    }
    // Source PositionChange(roomChange=true), including same-pose publication,
    // mutates RoomInfo.Entities. Flags/heading/footprint-only changes do not.
    // History is runtime-only and unavailable after saturation or a ring gap.
    std::optional<std::uint64_t> room_registration_revision() const noexcept;
    bool room_registration_events_since(std::uint64_t,
        std::vector<WorldRoomRegistrationEvent>&) const;
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
        EntityLayout layout, const std::vector<EntryPoint>& entry_points,
        std::optional<std::uint32_t> master_guid = {});
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
    const Entity* resolve_reference(WorldEntityReference) const;
    std::optional<bool> suppress_referenced_reset(WorldEntityReference) const;
    bool set_suppress_referenced_reset(WorldEntityReference,bool);
    std::optional<bool> skip_native_recovery_position(WorldEntityReference) const;
    bool set_skip_native_recovery_position(WorldEntityReference,bool);
    std::optional<WorldEntityReference> native_slot0(WorldEntityReference) const;
    // Publishes backing storage only; caller owns raw14/raw26 source-order writes.
    bool publish_native_slot0(WorldEntityReference parent,WorldEntityReference child);
    std::optional<EntityPosition> native_position(WorldEntityReference) const;
    // Caller owns proof of native sentinel XY. Only changes +110 while OOW;
    // it neither places an entity in the lot nor changes its heading/angle.
    bool set_native_out_of_world_level(WorldEntityReference,std::int16_t);
    bool publish_lookup_selector(WorldEntityReference,std::uint16_t selector,bool enabled);
    // Last publication only: callers must refresh from actual OD60/TTAB/group
    // owners before consuming. No general change-invalidation contract yet.
    std::optional<std::uint16_t> lookup_selectors(WorldEntityReference,std::uint16_t required) const;
    const Entity* frame_entity(const VmFrame&,VmEntityRole,bool live_only=false) const;
    bool bind_context_live(VmContext&) const;
    bool clear_bound_appearances(std::int16_t avatar);
    bool set_name(WorldEntityReference,const std::string&);
    // Exact dictionary membership; marked Dead remains indexed during Pre.
    bool is_live_reference(WorldEntityReference) const;
    bool reference_is_dead(WorldEntityReference) const;
    bool mark_dead(WorldEntityReference);
    bool remove_retaining_body(WorldEntityReference);
    // Synchronous source Delete continuation: numeric lookup is removed before
    // children are dropped. The opaque lease expires on every exit; failures
    // retain already-published effects and must not be saved as a full session.
    class RetiredContainer {
    public:
        WorldEntityReference parent() const noexcept {return parent_;}
    private:
        friend class World;
        const World* owner_=nullptr;
        WorldEntityReference parent_;
        std::shared_ptr<bool> active_;
        std::shared_ptr<std::set<WorldEntityReference>> released_;
    };
    bool remove_retaining_contents(WorldEntityReference,
        const std::function<bool(const RetiredContainer&)>&);
    bool retired_container_active(const RetiredContainer&) const;
    bool retired_child_authorized(const RetiredContainer&,WorldEntityReference) const;
    std::optional<std::size_t> retired_slot_count(const RetiredContainer&) const;
    std::optional<WorldEntityReference> retired_slot(const RetiredContainer&,std::size_t) const;
    bool copy_retired_position(const RetiredContainer&,WorldEntityReference child);
    bool detach_retained_slot(const RetiredContainer&,std::size_t,WorldEntityReference child);
    // Resolves reciprocal containment, including an exact retained parent.
    // No fallback to a replacement merely sharing its numeric ID.
    std::optional<WorldEntityReference> attachment_parent(WorldEntityReference child) const;

    bool collect_retained_bodies(const std::vector<WorldEntityReference>& roots,
        std::vector<WorldEntityReference>* collected=nullptr);
    // Live positive-ID enumeration, including entities with unknown definitions.
    std::optional<std::int16_t> next_entity(std::int16_t current,std::optional<EntityKind> kind={},
        bool include_ui_ghost=false) const;
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
    // Validate ALL exact baselines and stage allocation before changing any
    // member. Bank sizes must stay fixed. No entity/position/index publication;
    // existing Entity pointers and native traversal authority remain intact.
    bool publish_avatar_personal_states(const std::vector<AvatarPersonalStateChange>&);
    bool set_motive(std::int16_t id, std::int16_t index, std::int16_t value);
    bool add_bound_appearance(std::int16_t avatar,const std::string& name,std::optional<std::string> asset={});
    bool remove_bound_appearance(std::int16_t avatar,const std::string& name,std::optional<std::string> asset={});
    // TS1 continuous SetMotiveChange state belongs to the Caller avatar.
    // The setter returns true when the request is valid, including the first
    // pre-tick request that Simitone deliberately ignores.
    bool set_motive_change(std::int16_t id,std::int16_t motive,
                           std::int16_t per_hour_change,std::int16_t maximum);
    bool clear_motive_changes(std::int16_t id);
    bool tick_motive_changes();
    // Actual original constructor Simulate0: only this newly born body advances.
    bool tick_motive_changes(WorldEntityReference);
    bool has_motive_change(std::int16_t id,std::int16_t motive) const;
    bool declare_definition_word(std::int16_t id, std::int16_t index, std::int16_t value);
    // Runtime-only source SetObstacleStatic consumer. False retains the source
    // boolean mutation and reports incomplete external collection publication.
    using StaticFootprintObserver=std::function<bool(World&,std::int16_t,std::uint64_t,bool old_static,bool new_static)>;
    bool set_static_footprint_observer(StaticFootprintObserver);
    bool set_static_footprint(std::int16_t id, bool is_static,
        FootprintClassChangeSource=FootprintClassChangeSource::obstacle_static);
    using FootprintObserver=std::function<bool(World&,std::int16_t,std::uint64_t)>;
    bool set_footprint_observer(FootprintObserver);
    // Explicit source UpdateFootprint event after a HasZeroExtent flag change.
    // Bootstrap bank declaration and arbitrary writes do not emit this event.
    bool notify_footprint_changed(std::int16_t id);
    // VMEntity.SetValue(Flags) publishes its word/footprint/class effects
    // before avatar Reset on either Burning-bit transition. The host binds
    // its actual Thread/controller lifecycle; failure preserves earlier writes.
    using AvatarBurningObserver=std::function<bool(World&,const VmFrame& source,
        WorldEntityReference target,std::int16_t before,std::int16_t after)>;
    bool set_avatar_burning_observer(AvatarBurningObserver);
    // Explicit live-target SetFlag service. source is the real originating
    // frame, independent of target; no fabricated self-frame is required.
    bool set_entity_flags(const VmFrame& source,WorldEntityReference target,std::int16_t value);
    bool set_ui_ghost(std::int16_t id, bool is_ghost);
    std::optional<bool> dynamic_sprite_flag(std::int16_t id,std::uint16_t flag) const;
    bool set_dynamic_sprite_flag(std::int16_t id,std::uint16_t flag,bool enabled);
    bool set_avatar_kill_timeout(std::int16_t id, std::int32_t value);
    // Same tick is a no-op; uint32 wrapping follows the source scheduler.
    // Clock/stamp changes invalidate prepared publication proofs, not spatial
    // obstacle caches. No timestamp is inferred by enabling this model.
    bool set_lockout_count_clock(std::uint32_t tick) noexcept;
    std::optional<std::uint32_t> lockout_count_clock() const noexcept {return lockout_count_clock_;}
    // Refines unknown history. Conflicting known Thread/stamp evidence rejects;
    // passing an unknown stamp never erases an already known runtime write.
    bool admit_lockout_count_state(std::int16_t id,std::uint64_t generation,bool thread_present,
        std::optional<std::uint32_t> timestamp={}) noexcept;
    // Runtime-only VMEntity.SetValue(Direction) publication. The fixed setter
    // is minted after exact role/reference resolution and source quantization;
    // it owns only this heading change, never XYZ, containment or footprints.
    // It is synchronous, single-use and expires even if the host retains it.
    using DirectionPublisher=std::function<bool(World&,const VmFrame&,WorldEntityReference,
        std::uint8_t effective_heading,const std::function<bool()>& fixed_setter)>;
    bool set_direction_publisher(DirectionPublisher);
    // Native MT rotation adds the selected delta to every member directly;
    // member OD23 quantization belongs to the managed Direction path only.
    // Exact generation, actual publisher and all publication guards retained.
    bool publish_native_object_heading(WorldEntityReference,std::uint8_t);
    bool set_radian_direction(std::int16_t id, float radians);
    bool set_position(std::int16_t id, EntityPosition);
    bool attach(std::int16_t container, std::size_t slot, std::int16_t occupant);
    bool place_in_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                       std::optional<std::uint8_t> direction={});
    // Move an existing entity from its old container into an empty slot.
    // Used by TS1 Grab; rejects occupied slots and containment cycles atomically.
    bool move_to_slot(std::int16_t container,std::size_t slot,std::int16_t occupant,
                      std::optional<std::uint8_t> direction={});
    // Source transfer preserves each item's direction/radians while copying
    // only container XYZ to the entire contained subtree, including OOW.
    // Live, nonghost entities only; native World has no retained Dead records.
    // Unknown OOW object direction declines rather than inventing north.
    std::optional<PreparedSlotMove> prepare_move_to_slot(std::int16_t container,
        std::size_t slot,std::int16_t occupant) const;
    bool can_commit_move_to_slot(const PreparedSlotMove&) const noexcept;
    bool commit_move_to_slot(PreparedSlotMove&) noexcept;
    // Validate and allocate the positioned descendant list before detaching.
    // Preserves child orientations and leaves invalid placements unchanged.
    bool place_on_floor(std::int16_t id,EntityPosition);
    // Source SetPosition(OUT_OF_WORLD): detach root, retain physical children,
    // clear their coordinates and publish Room29 without invoking geometry.
    bool place_out_of_world(std::int16_t id,std::uint8_t direction=0);
    // One source PositionChange recursion step; copies parent XYZ only.
    // The caller owns unregister/recurse/register order for descendants.
    bool refresh_contained_position(std::int16_t child);
    // One SetIndivPosition step, without prematurely propagating descendants.
    // Null is the source OUT_OF_WORLD position; direction is a native notch.
    bool place_individual_on_floor(std::int16_t id,std::optional<EntityPosition>,std::uint8_t direction);
    // Native433ef5 scalar publication: XYZ/room only, preserving authored
    // heading and continuous avatar angle. Caller owns detach and descendant
    // ordering. clear_slot requires an already-uncontained root and publishes
    // native SlotNumber=-1 even if it started out of world/uncontained.
    bool publish_native_recovery_position(WorldEntityReference,std::optional<EntityPosition>,bool clear_slot);
    bool detach(std::int16_t container, std::size_t slot);
    // ObjectData26 reads current containment like2; its raw writable shadow and
    // immutable imported bank remain serialized (pinned GetValue/SetValue).
    std::optional<std::int16_t> read(const VmFrame&, std::uint8_t scope, std::int16_t index) const;
    // Same source value rules without committing logical read side effects.
    std::optional<std::int16_t> read_only(const VmFrame&, std::uint8_t scope, std::int16_t index) const;
    // Attribute growth validates budget and allocates before publication.
    // Allocation exceptions propagate without changing state or word accounting.
    bool write(const VmFrame&, std::uint8_t scope, std::int16_t index, std::int16_t value);
    // Fixed-bank signed maximum merge. Validates live generation, count and
    // callback admission before mutation; no growth/allocation or callback
    // occurs until the complete prefix is published. One spatial notification
    // follows a changed commit. Values must address count readable words.
    bool merge_attributes_max(WorldEntityReference,const std::int16_t* values,std::size_t count);
    // World must outlive bound VMs. Its address is stable (copy/move disabled).
    VmMemory vm_memory();
    std::size_t word_count() const { return words_; }
    // Changes whenever entity state relevant to live room/route obstacles changes.
    std::uint64_t spatial_revision() const noexcept { return spatial_revision_; }
    using SpatialChangeObserver=std::function<void(World&,std::uint64_t)>;
    // Runtime invalidation only; never serialized and never vetoes publication.
    bool set_spatial_change_observer(SpatialChangeObserver);
private:
    std::optional<std::int16_t> read_impl(const VmFrame&,std::uint8_t,std::int16_t,bool commit_logical_reads) const;
    struct OriginalLoadNode {WorldEntityReference previous{},next{};};
    using OriginalLoadNodes=std::map<WorldEntityReference,OriginalLoadNode>;
    struct OriginalLoadState {
        std::shared_ptr<bool> active;
        OriginalLoadNodes nodes,retained_nodes;
        std::vector<WorldEntityReference> people;
        WorldEntityReference first{},last{};
        std::size_t limit=0;
    };
    std::optional<OriginalLoadState> original_load_;
    bool original_load_binding_eligible_=true;
    bool original_load_active() const noexcept;
    bool stage_original_load_insert(WorldEntityReference,OriginalLoadNodes&) const;
    void publish_original_load_insert(WorldEntityReference,OriginalLoadNodes&,bool append) noexcept;
    void unlink_original_load(WorldEntityReference,bool retain_body=false) noexcept;
    void invalidate_original_load() noexcept;
    bool native_slot0_detached(WorldEntityReference) const;
    bool native_hierarchy_would_cycle(WorldEntityReference parent,WorldEntityReference child) const;
    bool publish_snapshot_impl(const WorldSnapshot&,bool runtime_constructor);
    bool stage_slot_growth(const Entity&,std::size_t,std::vector<std::int16_t>&,std::size_t extra_words=0) const;
    // Physical containment publishes native ordinals1+ only for a body whose
    // native backing is known. Fixed ObjectData words do not use bank_words.
    bool stage_native_physical_parent(const Entity&,std::map<std::int16_t,std::int16_t>&,
        std::size_t extra_words=0) const;
    void publish_native_physical_parent(Entity&,std::map<std::int16_t,std::int16_t>&,
        std::int16_t slot,std::int16_t parent) noexcept;
    using PhysicalDetachNodes=std::vector<std::pair<std::int16_t,std::map<std::int16_t,std::int16_t>>>;
    bool stage_physical_detaches(const std::vector<std::int16_t>&,PhysicalDetachNodes&) const;
    void publish_physical_detach_nodes(PhysicalDetachNodes&) noexcept;
    std::vector<std::int16_t> position_tree(std::int16_t) const;
    std::vector<PreparedSlotMove::PositionUpdate> position_updates(const std::vector<std::int16_t>&,EntityPosition) const;
    bool stage_rooms(std::vector<PreparedSlotMove::PositionUpdate>&) const;
    void apply_position(std::vector<PreparedSlotMove::PositionUpdate>&) noexcept;
    void publish_room(PreparedSlotMove::PositionUpdate&) noexcept;
    bool reject_room_reentry() const noexcept;
    void mark_spatial_change() noexcept;
    void notify_spatial_change() noexcept;
    void mark_room_registration(std::int16_t,std::uint64_t,
        std::optional<EntityPosition>,std::optional<EntityPosition>,bool all_rooms=false) noexcept;
    std::map<std::int16_t, Entity> entities_;
    std::map<WorldEntityReference,Entity> retained_entities_;
    std::map<WorldEntityReference,std::weak_ptr<bool>> retained_container_leases_;
    Entity* frame_entity_mutable(const VmFrame&,VmEntityRole);
    WorldLimits limits_;
    AttributePolicy attribute_policy_;
    std::size_t words_ = 0;
    std::uint64_t generation_sequence_ = 0;
    std::uint64_t entity_visibility_revision_ = 0; // Runtime identity guard, not wire state.
    std::uint64_t spatial_revision_ = 0;
    // Invalidates prepared references even when publish_snapshot receives the
    // same serialized spatial revision. It is intentionally not save state.
    mutable std::uint64_t slot_publication_epoch_ = 0;
    std::optional<std::uint32_t> lockout_count_clock_;
    DefinitionWordResolver definition_word_resolver_;
    RoomResolver room_resolver_;
    AvatarOutfitResolver avatar_outfit_resolver_;
    mutable bool resolving_outfit_=false,outfit_resolution_reentered_=false;
    std::uint64_t room_capability_revision_=0;
    mutable bool resolving_rooms_=false,room_resolution_reentered_=false;
    std::uint64_t room_registration_serial_=0;
    std::vector<WorldRoomRegistrationEvent> room_registration_history_;
    std::size_t room_registration_cursor_=0;
    DirectionPublisher direction_publisher_;
    bool publishing_direction_=false;
    bool write_direction(const VmFrame&,WorldEntityReference,std::int16_t value,bool retained,bool quantize=true);
    CurrentValueReadResolver current_value_read_resolver_;
    CurrentValueWriteObserver current_value_write_observer_;
    mutable bool resolving_current_value_=false,current_value_reentry_=false;
    bool write_current_value(WorldEntityReference,std::int16_t,std::int16_t);
    StaticFootprintObserver static_footprint_observer_;
    bool notifying_static_footprint_=false;
    FootprintObserver footprint_observer_;
    bool notifying_footprint_=false;
    AvatarBurningObserver avatar_burning_observer_;
    unsigned avatar_burning_depth_=0;
    bool write_flags(const VmFrame&,Entity&,std::int16_t,bool retained);

    SpatialChangeObserver spatial_change_observer_;
    bool notifying_spatial_change_=false;
};
} // namespace odh
