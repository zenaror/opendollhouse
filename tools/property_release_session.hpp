// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "community_lot_store.hpp"
#include "house_panel_finance.hpp"
#include "odh/family_overlay.hpp"
#include "odh/session_bundle.hpp"
#include <functional>

namespace odh_host {
enum class PropertyReleaseTraversalProfile : std::uint16_t { unknown=0, original_linked_head=1, pinned_vm_id_order=2 };
enum class PropertyReleaseLinkStatus { member,end,unavailable };
struct PropertyReleaseLink {
    PropertyReleaseLinkStatus status=PropertyReleaseLinkStatus::unavailable;
    odh::WorldEntityReference reference{};
};
// Borrowed ONLY for prepare_property_release_session. The host owns a private
// restored runtime, all real VMs/resources/registries and this operation's lease.
// Failure requires discarding that whole runtime; this API promises no rollback.
struct PropertyReleaseRuntime {
    odh::World& world;
    const odh::DefinitionIndex& definitions;
    // Authority survives this operation's expected deletions/constructions;
    // it is not a frozen World-revision comparison or an unconditional true.
    std::function<bool()> source_still_matches;
    std::function<PropertyReleaseLink()> first;
    std::function<PropertyReleaseLink(odh::WorldEntityReference)> next;
    // Actual source Delete/Pre/retirement/drop, completed synchronously without
    // Main/ticks. On true the exact old generation must no longer be indexed.
    std::function<bool(odh::WorldEntityReference,std::string&)> delete_entity;
    // SAME current getter-bound World/policy/terrain/price owner, before+after.
    std::function<odh::PropertyValuation()> valuation;
    std::function<std::int32_t()> current_cash;
    // Shared typed neighborhood owner, when present; deletion callbacks may
    // update it and it must join the same overlay CAS rather than disappear.
    std::function<std::optional<odh::TypeAttributesSnapshot>()> export_type_attributes;
    // Typed capture AFTER real deletion. Build from current World/object VMs,
    // scheduler, globals, groups and real creation/removal/retirement history;
    // never erase household arrays in a full save as a stand-in for retirement.
    std::function<odh::ActorlessObjectTradeState()> export_objects;
    std::function<odh::ArchitectureSnapshot()> export_architecture;
    // Real resource reconstruction/activation validation of the entire document,
    // including actor retirement provenance. No UI/old resident VM is reused.
    std::function<void(const CommunityLotDocument&)> validate_reopen;
    PropertyReleaseTraversalProfile profile=PropertyReleaseTraversalProfile::unknown;
};
class PreparedPropertyRelease {
public:
    const CommunityLotDocument& document()const{return document_;}
    const odh::FamilyOverlay& replacement()const{return replacement_;}
    const odh::FamilyOverlay& expected_overlay()const{return expected_;}
    const odh::FamilyPropertyRelease& release()const{return release_;}
    std::int32_t refund()const{return refund_;}
    std::int32_t source_cash()const{return source_cash_;}
    std::uint64_t deleted_roots()const{return deleted_roots_;}
private:
    PreparedPropertyRelease()=default;
    CommunityLotDocument document_;
    odh::FamilyOverlay expected_,replacement_;
    odh::FamilyPropertyRelease release_;
    std::int32_t refund_=0,source_cash_=0;
    std::uint64_t deleted_roots_=0;
    friend PreparedPropertyRelease prepare_property_release_session(
        const odh::NeighborhoodFamilySource&,const odh::FamilyOverlay&,std::uint16_t,
        const std::vector<std::uint8_t>&,const odh::SessionResourceIdentity&,
        const CommunityLotIdentity&,PropertyReleaseRuntime,std::uint64_t);
};
// Authenticates exact head/lease/cash/roster before any runtime effect. Traversal
// restarts at the real HEAD after every source deletion (004273c8). No file writes.
PreparedPropertyRelease prepare_property_release_session(
    const odh::NeighborhoodFamilySource&,const odh::FamilyOverlay&,std::uint16_t family,
    const std::vector<std::uint8_t>& current_checkpoint,const odh::SessionResourceIdentity& expected_resources,
    const CommunityLotIdentity&,PropertyReleaseRuntime,std::uint64_t operation_budget=65536);
// Blob first, exact whole-overlay CAS second. No legacy community HEAD is used.
// A stale/conflicting CAS may leave an orphan blob without changing family or
// lot authority. Errors after rename require the exact readback status below.
struct PropertyReleasePublication {
    StoredCommunityLot stored;
    // Exact reread proved publication after the store reported an error.
    // The host must retain the committed state and report uncertain durability,
    // never repeat the eviction/refund or describe it as unchanged.
    bool durability_uncertain=false;
};
PropertyReleasePublication publish_property_release_session(const odh::NeighborhoodFamilySource&,
    const CommunityLotStoreOptions&,const PreparedPropertyRelease&);
}
