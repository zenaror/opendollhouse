// SPDX-License-Identifier: MPL-2.0
// FreeSO VMMultitileGroup.ChangePosition / VMGameObject.PositionChange,
// revision 7e76c1960252ddd033d8937d5edb47d326f5b69c.
#pragma once
#include "odh/object_position_valid.hpp"
#include "odh/object_group.hpp"
namespace odh {
enum class ObjectPositionChangeStatus { success,blocked,unsupported };
enum class ObjectPositionChangePhase { pre,post };
struct ObjectPositionChangeResult {
    ObjectPositionChangeStatus status=ObjectPositionChangeStatus::unsupported;
    ObjectPositionValidResult placement;
    bool effects_started=false,position_published=false;
    std::string diagnostic;
};
// Field-publication portion of source42296a. A delta is applied relative to each live
// member's own heading; refusal subtracts it from the then-current live list.
// The selected body and actual native head are independent of BaseObject.
// This is NOT the Rotate(step) driver: its selected OD23 multiplication,
// second virtual54 and conditional virtual58 remain the caller's policy.
struct NativeObjectGroupRotation {
    WorldEntityReference selected{},head{};
    std::uint8_t delta=0;
};
struct ObjectPositionChangeServices {
    std::function<std::optional<std::int16_t>()> story_count;
    // Complete group/ordinary-object proof: SINGLE offset0, no GhostImage,
    // architectural portal/window. Containers/children have separate reciprocal
    // identity proofs; each physical child must satisfy this same profile. No GUID gate.
    std::function<std::optional<bool>(ObjectPositionIdentity)> admit_single_ordinary;
    // Actual source-load admission, independent of ordinary singleton move
    // admission. Certifies loaded identity/resource/group and real callbacks;
    // it must not manufacture a placement request or execute PositionValid.
    std::function<std::optional<bool>(ObjectPositionIdentity)> admit_loaded_entity;
    std::function<std::optional<std::uint16_t>(const EntityPosition&)> room_at;
    // Exact VMContext operations, including footprint/query/RoomInfo membership,
    // registration history and source lighting/room-score updates. A true result
    // certifies completion, NOT an entry-point BHAV's returned boolean.
    std::function<bool(ObjectPositionIdentity,bool room_change)> unregister_position;
    std::function<bool(ObjectPositionIdentity,std::uint16_t room,bool room_change)> register_position;
    // SetIndivPosition's PlaceInSlot(...,0,cleanOld=false). The service owns real
    // slot publication/carry effects. False leaves already-published effects.
    std::function<bool(ObjectPositionIdentity,ObjectPositionIdentity)> place_in_slot0;
    // Failed ChangePosition restores the exact old slot with cleanOld=false,
    // before recursive PositionChange. Must preserve the child's heading.
    std::function<bool(ObjectPositionIdentity,ObjectPositionIdentity,std::size_t)> restore_container;
    // Exact source Pre detach, including a retired container still holding
    // this child. Optional: ordinary indexed containment uses World::detach.
    std::function<bool(ObjectPositionIdentity,ObjectPositionIdentity,std::size_t)> detach_container;
    // Exact SetWallUse, including live EP6 adjacency and OccupiedWalls changes.
    std::function<bool(ObjectPositionIdentity,bool set,bool exclusive)> wall_use;
    // VMGameObject.PositionChange's architectural door/window branch: load
    // dynamic wall style where needed and assign the required wall faces.
    // Invoked by source-load replay before SetWallUse, only for FlagField2
    // architectural bits. Ordinary placement still uses its existing admission.
    std::function<bool(ObjectPositionIdentity)> architectural_wall_style;
    std::function<bool(ObjectPositionIdentity,const EntityPosition&,bool set)> object_supported;
    // These execute actual EP8/EP9 with entity registers and source contexts;
    // false means unavailable/incomplete, never an authored BHAV false result.
    std::function<bool(ObjectPositionIdentity)> dynamic_multitile,entry_point9;
    // Optional actual SetIndivPosition publication boundary, after Pre/check
    // and before Post. Owns direction-word write and scalar root publication;
    // it must use the supplied exact identity/pose, not move descendants.
    // Unbound preserves World publication. Core revalidates physical output.
    std::function<bool(ObjectPositionIdentity,const std::optional<EntityPosition>&,std::uint8_t)> publish_position;
    // Optional synchronous authority around apply/apply_existing's source
    // Pre and Post only. Call operation exactly once inside the corresponding
    // host guard. PositionValid/EP5 and scalar publication remain separate.
    // Public pre_position_change/position_change already have caller-owned
    // guards and deliberately do not invoke this hook a second time.
    std::function<bool(ObjectPositionIdentity,ObjectPositionChangePhase,
        const std::function<bool()>& operation)> lifecycle_phase;
    // Optional resource-backed physical-group authority. A provided resolver
    // must return the complete ordered group or unknown; never infer siblings
    // from positions or admit an abstract master. Offsets are VM units (16/tile).
    std::function<std::optional<ObjectGroup>(ObjectPositionIdentity)> group;
    // Separate ordinary physical-member proof for group movement: exact live
    // resource/entry points, no GhostImage, architectural portal/window or
    // dynamic-multitile split. Does not weaken legacy singleton admission.
    std::function<std::optional<bool>(ObjectPositionIdentity)> admit_group_member;
    // Source extra Footprint.Unregister when a dynamic, same-room member will
    // enter a support slot. Separate from the broader Pre unregister operation.
    std::function<bool(ObjectPositionIdentity)> unregister_support_footprint;
    // Only definition_guid==0 uses this imported-group authority. Authenticate
    // the complete ordered source manifest: base, offsets, generations and each
    // part's exact resource provenance. IDs or mutable price origin alone do
    // not prove membership. No World mutation or I/O is authorized here.
    // Constructed nonzero-GUID groups retain strict common-master checks.
    std::function<bool(const ObjectGroup&)> authenticate_imported_group;
    // Explicit owned live-association authority. Unlike an imported static
    // manifest, this may change only through the represented dynamic service.
    std::function<bool(const ObjectGroup&)> authenticate_live_group;
    std::function<std::optional<std::vector<ObjectPositionIdentity>>(ObjectPositionIdentity)> split_dynamic_group;
    std::function<bool(ObjectPositionIdentity)> update_dynamic_group_flags;
    // Native-known absolute group placement only. These bind the same actual DynamicObjectGroups
    // owner; begin publishes ALL headings and recomputes current1a0 before any
    // placement check. Rollback walks current membership and recomputes again.
    std::function<std::optional<NativeObjectGroupRotation>(ObjectPositionIdentity,std::uint8_t)> begin_native_rotation;
    std::function<bool(const NativeObjectGroupRotation&)> rollback_native_rotation;
    std::function<std::optional<NativeObjectGroupOffset>(WorldEntityReference)> native_current_offset;
    std::function<NativeObjectGroupLink(WorldEntityReference)> native_group_head,native_group_next;
    // Original class dispatch only (absolute placement keeps its old contract):
    // EP10-all, remove-all, World447d6b; native58 posts each member without base
    // EP9, then EP9-all, followed by World44756e when captured anchor was outside.
    // Hosts must supply actual entry/editor/controller owners, never no-op proof.
    std::function<bool(ObjectPositionIdentity)> native_remove_entry10;
    std::function<bool(ObjectPositionIdentity)> native_group_removed;
    std::function<bool(ObjectPositionIdentity)> native_group_added;
    // Optional actual post-Init SINGLE constructor proof. Used only for the
    // root of apply(), at capture AND after PositionValid/EP5. It may admit
    // lighting only when the bound Pre/Post services own the real complete
    // event registry and lighting requests. No fallback after refusal. Existing
    // moves and contained children retain admit_single_ordinary unchanged.
    std::function<std::optional<bool>(ObjectPositionIdentity)> admit_single_constructed;

};
// Synchronous shared native positioning stages. The loan cannot escape its
// driver. Repeated check54 executes the actual PositionValid pipeline anew;
// publish58 does not rerun Pre or a placement check. Completed effects survive
// unsupported/blocked results. Absolute positioning and Rotate(step) share it.
class NativeObjectPositionStages {
public:
    bool pre(){return pre_();}
    ObjectPositionValidResult check54(){return check_();}
    bool publish58(){return publish_();}
    bool restore_after_refusal(){return restore_();}
    std::optional<NativeObjectGroupRotation> rotate_to(std::uint8_t heading){return rotate_(heading);}
    bool rollback(const NativeObjectGroupRotation& token){return rollback_(token);}
    NativeObjectPositionStages(const NativeObjectPositionStages&)=delete;
    NativeObjectPositionStages& operator=(const NativeObjectPositionStages&)=delete;
private:
    friend class ObjectPositionChange;
    NativeObjectPositionStages()=default;
    std::function<bool()> pre_,publish_,restore_;
    std::function<ObjectPositionValidResult()> check_;
    std::function<std::optional<NativeObjectGroupRotation>(std::uint8_t)> rotate_;
    std::function<bool(const NativeObjectGroupRotation&)> rollback_;
};
class ObjectPositionChange {
public:
    ObjectPositionChange(World& world,ObjectPositionValid& placement,ObjectPositionChangeServices services)
        :world_(world),placement_(placement),services_(std::move(services)){}
    // New ordinary objects: certified complete physical groups when the group
    // provider is present, otherwise the singleton constructor profile (ordinary
    // unless the explicit constructed-root proof is supplied). No effect rollback.
    ObjectPositionChangeResult apply(const ObjectPositionValidRequest&);
    // Same source lifecycle for an existing ordinary group/singleton, including a held
    // root and its bounded ordinary-object containment tree.
    // A blocked check re-registers the original pose and runs its placement
    // callbacks; completed check effects survive. Sentinel -32768/-32768/1
    // is the actual source OUT_OF_WORLD request, not an invalid floor point.
    ObjectPositionChangeResult apply_existing(const ObjectPositionValidRequest&);
    // Direct VMMultitileGroup.ChangePosition (construction/buy), without the
    // VMEntity.SetPosition dynamic single-member split.
    ObjectPositionChangeResult apply_existing_group(const ObjectPositionValidRequest&);
    // Exact cleanOld=true container-transfer lifecycle. The caller owns the
    // authenticated slot/carry plan between these calls. No synthetic opcode
    // or PositionValid check is introduced. Completed effects are retained.
    bool pre_position_change(ObjectPositionIdentity,bool room_change,const std::function<bool()>& authority);
    bool position_change(ObjectPositionIdentity,bool room_change,const std::function<bool()>& authority);
    // Source class virtual50/54/58 stages, no autonomous rotation/rollback.
    // original_class=true selects class entry/World notifications; outside is
    // the authenticated source422665 predicate for the CAPTURED selected pose.
    ObjectPositionChangeResult with_native_position_stages(const ObjectPositionValidRequest&,
        bool original_class,bool captured_outside,
        const std::function<ObjectPositionChangeStatus(NativeObjectPositionStages&)>& driver);
    // Last boolean lifecycle failure only; no state rollback or new callback.
    const std::string& diagnostic() const noexcept {return diagnostic_;}
    // VM.Load root PositionChange(context,noEntryPoint=true). The caller owns
    // the authentic ordered source entity list and calls this only for entries
    // that are currently uncontained. Recurses current children in slot order;
    // suppresses ONLY base EP9, preserving wall use, Category8 and dynamic EP8.
    // No root unregister, PositionValid or rollback. Partial effects survive
    // false. This per-root replay is bounded to 4096 recursive calls.
    bool loaded_position_change(ObjectPositionIdentity,const std::function<bool()>& source_load_authority);
private:
    ObjectPositionChangeResult apply_native_group(const ObjectPositionValidRequest&,bool require_new);
    ObjectPositionChangeResult run_native_stages(const ObjectPositionValidRequest&,bool require_new,bool original_class,
        bool captured_outside,const std::function<ObjectPositionChangeStatus(NativeObjectPositionStages&)>&);
    ObjectPositionChangeResult apply_group(const ObjectPositionValidRequest&,bool require_new);
    ObjectPositionChangeResult apply_single(const ObjectPositionValidRequest&,bool require_new);
    World& world_;ObjectPositionValid& placement_;ObjectPositionChangeServices services_;bool executing_=false;
    std::string diagnostic_;
};
} // namespace odh
