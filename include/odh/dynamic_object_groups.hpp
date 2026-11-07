// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/object_group.hpp"
#include "odh/object_position_valid.hpp"
#include "odh/object_value_registry.hpp"
#include "odh/object_position_change.hpp"
namespace odh {
// Exact live association port of pinned VMEntity.UpdateDynamicMultitile and
// VMMultitileGroup.RemoveObject/Combine. Resource reads and entry execution are
// supplied by the same World/thread owner; no file access occurs here.
struct DynamicObjectGroupServices {
    std::function<std::optional<ObjectGroup>(ObjectPositionIdentity)> source_group;
    std::function<std::optional<ObjectGroupMemberOrigin>(ObjectPositionIdentity)> origin;
    std::function<std::optional<ObjectGroupMember>(ObjectPositionIdentity)> definition_offset;
    std::function<bool(ObjectPositionIdentity,std::int16_t)> execute_entry8;
    std::function<void()> changed;
    std::function<bool()> authority;
    std::function<bool(ObjectPositionIdentity,std::uint8_t)> publish_direction;
    // Raw native field4c publication, without managed per-member OD23 rounding.
    // Must retain the real World heading/registry publication owner.
    std::function<bool(WorldEntityReference,std::uint8_t)> publish_native_direction;
};
class DynamicObjectGroups {
public:
    DynamicObjectGroups(World& world,ObjectValueRegistry& values,ObjectGroupSnapshot& groups,DynamicObjectGroupServices services)
        :world_(world),values_(values),groups_(groups),services_(std::move(services)){}
    // Explicit actual-source admission. Rows preserve native MAP/constructor
    // order, leader first, and carry BOTH exact triples. No sorted legacy,
    // BaseObject, positions or common master can infer this metadata.
    bool bind_original_native_group(ObjectPositionIdentity target,WorldEntityReference head,
        const std::vector<NativeObjectGroupSourceMember>& rows);
    // Constructors bind their actual produced fields through the same atomic
    // admission; this never fabricates fields for an unknown restored group.
    bool bind_native_created_group(ObjectPositionIdentity target,WorldEntityReference head,
        const std::vector<NativeObjectGroupSourceMember>& rows);
    bool collect_retired_native_members();
    NativeObjectGroupLink group_head(WorldEntityReference)const;
    NativeObjectGroupLink group_next(WorldEntityReference)const;
    // Raw43532b: OBJD signed InteractionGroupID at byte16/scope21 word8,
    // gated by MasterID at byte20/word10, then first negative partner in actual
    // native head/NEXT order. Non-MT returns self; unknown MT ownership is
    // unavailable, never reconstructed from BaseObject, SubIndex or sorted IDs.
    // Read-only, including during dispatch when the host authority allows it.
    std::optional<WorldEntityReference> resolve_interaction_group(WorldEntityReference);
    std::optional<NativeObjectGroupOffset> native_offset(WorldEntityReference)const;
    bool recompute_current_offsets(WorldEntityReference leader);
    std::optional<NativeObjectGroupRotation> begin_native_rotation(ObjectPositionIdentity,std::uint8_t absolute_heading);
    bool rollback_native_rotation(const NativeObjectGroupRotation&);
    bool ensure(ObjectPositionIdentity); // Authenticate and own the current live association.
    // Nullopt means unsupported; empty means no split. Remaining identities
    // must receive update(id,false) after placement, including authored refusal.
    std::optional<std::vector<ObjectPositionIdentity>> split_for_move(ObjectPositionIdentity);
    // Call after the caller's unique World retirement and retire_member. This
    // removes physical membership only; it never retires the value twice.
    bool erase_after_retire(ObjectPositionIdentity);
    bool update(ObjectPositionIdentity,bool combine=true);
    const std::string& diagnostic()const{return diagnostic_;}
private:
    World& world_;ObjectValueRegistry& values_;ObjectGroupSnapshot& groups_;DynamicObjectGroupServices services_;
    std::string diagnostic_;std::size_t calls_=0,depth_=0;
    ObjectGroup* find(ObjectPositionIdentity);
    const ObjectGroup* find(ObjectPositionIdentity)const;
    std::optional<ObjectGroup> prepare_association(ObjectPositionIdentity);
    bool bind_native_group(ObjectPositionIdentity,WorldEntityReference,
        const std::vector<NativeObjectGroupSourceMember>&,ObjectGroupMemberSource);
    bool combine(ObjectPositionIdentity,ObjectPositionIdentity);
    bool update_impl(ObjectPositionIdentity,bool);
    bool current()const;
    bool rotate_live_native(WorldEntityReference,std::uint8_t delta);
    bool fail(const char*);
    void changed();
};
}
