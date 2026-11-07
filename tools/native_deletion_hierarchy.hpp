// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "native_recovery_placement.hpp"
#include "odh/ts1_placement.hpp"

namespace odh_host {
struct NativeHierarchySnapshot;
enum class NativeDeletionSpatialBackend {runtime_flat_registry,original_native_floor_heads,unindexed_constructor};
// One actual Delete lease. Runtime uses the complete event-owned Simitone
// registry LIST, allowing multiple exact refs per cell; original import recovery
// instead supplies its strict native floor-head owner. Neither invents head
// order from IDs. Actual source child-drop callbacks preserve these profiles.
class NativeDeletionHierarchy {
public:
    using Ref=odh::WorldEntityReference;
    ~NativeDeletionHierarchy();
    using UnindexedConstructionOwner=std::function<bool(Ref)>;
    using DropOwner=std::function<bool(const odh::World::RetiredContainer&,NativeDeletionHierarchy&,std::string&)>;
    static std::unique_ptr<NativeDeletionHierarchy> prepare(SessionObjectPositionContext,
        SessionObjectPositionServices,Ref,std::string&,DropOwner={},
        std::shared_ptr<NativeFloorHeads> existing={},NativeFloorHeads::PendingUnlinkedSource pending={},
        UnindexedConstructionOwner unindexed_construction_owner={});
    // Runs every real Pre (including the source's second Pre), but performs
    // native unlink/promotion exactly once, inside the physical-detach seam.
    bool before_position_change(std::string&);
    // After actual indexed removal, with the original retained body accessible.
    // Does not remove, drop physical contents, run EP10 or publish scalar OOW.
    bool finish(std::string&);
    // Called INSIDE World's actual retirement callback, instead of calling the
    // source drop owner directly. The supplied owner retains source_container_drop
    // and wraps only these actual copy/out_of_world/find_location callbacks.
    // First statement in World's remove_retaining_contents callback, before
    // group retirement/queries. Reused by drop_retired_contents without a
    // duplicate forget/begin. Must be paired with finish (or failure discard).
    bool begin_indexed_retirement(const odh::World::RetiredContainer&,std::string&);
    // Read-only receipt fact, NOT execution authority. Provider must still
    // prove queue/local ownership and active exact operation before exposing it.
    bool certified_detached()const;
    bool drop_retired_contents(const odh::World::RetiredContainer&,std::string&);
    bool copy_retired_child_position(const odh::World::RetiredContainer&,Ref,
        const std::function<bool()>& actual_copy,std::string&);
    odh::Ts1PlacementResult place_retired_child(const odh::World::RetiredContainer&,Ref,
        const std::function<odh::Ts1PlacementResult()>& actual_placement,std::string&);
    const std::shared_ptr<NativeFloorHeads>& floor_heads()const{return floors_;}
    NativeDeletionSpatialBackend spatial_backend()const{return existing_floors_?NativeDeletionSpatialBackend::original_native_floor_heads:unindexed_constructor_?NativeDeletionSpatialBackend::unindexed_constructor:NativeDeletionSpatialBackend::runtime_flat_registry;}
    Ref target()const{return target_;}
    bool membership_forgotten()const{return retired_forgotten_;}
    bool contents_completed()const{return drop_completed_;}
    bool effects_started()const{return effects_;}
    unsigned pre_calls()const{return pre_calls_;}
    unsigned unlink_calls()const{return unlink_calls_;}
private:
    struct Proof {
        Ref reference{};
        std::optional<odh::EntityPosition> pose;
        std::optional<Ref> child,parent;
        std::optional<odh::Attachment> attachment;
        std::int16_t raw_slot=0,raw_parent=0;
        std::vector<std::int16_t> slots;
    };
    NativeDeletionHierarchy(SessionObjectPositionContext,SessionObjectPositionServices,Ref);
    bool current()const;
    bool snapshot(Ref,Proof&,std::string&)const;
    bool matches(const Proof&,bool physical_slots,std::string&)const;
    bool detached(std::string&)const;
    bool unindexed_constructor_ready(std::string&)const;
    bool flat_registry_ready(std::string&)const;
    bool flat_position_proven(Ref,std::string&)const;
    bool known_absent_oow(Ref,std::string&)const;
    bool flat_unregistered(Ref,std::string&)const;
    bool flat_cleanup_legacy(const NativeHierarchySnapshot&,Ref,const std::optional<Proof>&,std::string&);
    SessionObjectPositionContext context_;
    SessionObjectPositionServices services_;
    Ref target_{};
    std::shared_ptr<NativeFloorHeads> floors_;
    std::unique_ptr<NativeRecoveryPlacement> placement_;
    std::vector<Ref> closure_;
    std::vector<Proof> proofs_;
    DropOwner drop_owner_;
    UnindexedConstructionOwner unindexed_construction_owner_;
    bool unindexed_constructor_=false;
    std::shared_ptr<NativeFloorHeads> existing_floors_;
    NativeFloorHeads::PendingUnlinkedSource pending_unlinked_source_;
    std::optional<odh::EntityPosition> detached_pose_;
    std::optional<odh::World::RetiredContainer> retirement_token_;
    bool retired_forgotten_=false,drop_active_=false,drop_completed_=false;
    bool entered_=false,unlinked_=false,finished_=false,failed_=false,effects_=false;
    unsigned pre_calls_=0,unlink_calls_=0;
};
}
