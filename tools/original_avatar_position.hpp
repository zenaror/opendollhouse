// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "object_trade_session_adapter.hpp"
#include "odh/avatar_position_valid.hpp"
#include "odh/ts1_placement.hpp"

namespace odh_host {
struct OriginalAvatarPositionAttempt {
    odh::Ts1PlacementResult result;
    bool effects_started=false;
};
// Actual shared Pre/check/publish/Post placement, with original-import authority.
// This does not reproduce Person.Reset's query-only +54 or its search order.
// A blocked attempt runs Post at the old pose; detached containment is not
// rolled back. Completed World, register and registry effects remain visible.
OriginalAvatarPositionAttempt place_original_avatar_position(
    const odh::LiveOriginalRecoveryPlacementSource&,const odh::EntityPosition&,
    odh::AvatarPositionValid&,SessionObjectPositionContext,bool check_before_publish=true);

// Actual conditional owners from4331d4. nullopt from conditions is unknown;
// false means the host proved that particular branch inactive. These are not
// defaults inferred from the absence of a presentation/lighting callback.
struct OriginalAvatarRemovalConditions {
    bool presentation_before, tile_use, neighbor_updates, type6, visual_controller, flagfield_event;
};
struct OriginalAvatarRemovalServices {
    // Read afresh at every native branch, not a frozen admission plan. The
    // provider must observe current controls after preceding callback effects.
    std::function<std::optional<OriginalAvatarRemovalConditions>(odh::ObjectPositionIdentity)> conditions;
    // A read-only complete resource/controller admission before EP10 or writes.
    std::function<bool(odh::ObjectPositionIdentity,std::string&)> admit;
    // Native422d79 receiver-owned Thread/TEMP defaults, NULL arguments. In
    // particular EP9 receives the captured OLD PARENT, never this person.
    std::function<bool(odh::ObjectPositionIdentity,std::uint16_t,std::string&)> entry;
    //4346fc/434f7b: actual renderer invalidation/timestamps, after core records
    // MovedSelf on the top owner and its existing containment descendants.
    std::function<bool(odh::ObjectPositionIdentity top_owner,odh::ObjectPositionIdentity person,std::string&)> move_bookkeeping;
    std::function<bool(odh::ObjectPositionIdentity,const odh::EntityPosition&,std::string&)> presentation_before;
    std::function<bool(odh::ObjectPositionIdentity,const odh::EntityPosition&,std::string&)> tile_use;
    //43329e..4332b9 emits40766a(F2,ObjectID) iff raw Flags2/ObjectData40 bit400.
    std::function<bool(odh::ObjectPositionIdentity,std::string&)> flagfield_event;
    //457da4 gets/materializes the real entry table;4561a0(26) is SitAction.
    // Nonzero SitAction enables4336c1 neighbor updates; then after old-parent EP9
    // the master-resolved OBJD.BuildModeType6 enables433186, followed by the
    // visual-controller virtual+54(1), in source order.
    std::function<bool(odh::ObjectPositionIdentity,const odh::EntityPosition&,std::string&)> neighbor_updates;
    std::function<bool(odh::ObjectPositionIdentity,const odh::EntityPosition&,std::string&)> type6;
    std::function<bool(odh::ObjectPositionIdentity,std::string&)> visual_controller;
    //447d6b because this fixed profile is RemoveFromWorld(1).
    std::function<bool(odh::ObjectPositionIdentity,std::string&)> world_removed;
    // Only needed when World's optional pose is already null. Supply proven
    // native f4/f8/110 + real heading, e.g. this operation's previous result.
    std::function<std::optional<odh::EntityPosition>()> native_pose;
    // Bound during complete native recovery to the actual floor-head index.
    // The ordinary registry tracks all occupants and is not this index.
    std::function<bool(odh::WorldEntityReference,const odh::EntityPosition&,std::string&)> native_floor_unregister;
    std::function<bool(odh::WorldEntityReference,const odh::EntityPosition&,std::string&)> native_floor_promote;
};
struct OriginalAvatarRemovalAttempt {
    bool completed=false,effects_started=false;
    std::optional<odh::EntityPosition> native_pose;
    std::optional<odh::WorldEntityReference> old_parent;
    std::string diagnostic;
};
using OriginalEntityRemovalServices=OriginalAvatarRemovalServices;
using OriginalEntityRemovalAttempt=OriginalAvatarRemovalAttempt;
// Shared native4331d4 for actual object/avatar bodies. The context's source
// closure owns original-load/class authority; this captures the exact live
// Thread/run and revalidates it before every borrowed World access.
OriginalEntityRemovalAttempt remove_original_entity_from_world(
    odh::WorldEntityReference,SessionObjectPositionContext,const OriginalEntityRemovalServices&);
// Native4331d4(1), not generic PrePositionChange. Directly owns real registry
// unregister, reciprocal detach, scalar OOW publication and Flags8 clearing;
// completed effects survive failure and invalidate the partial registry.
OriginalAvatarRemovalAttempt remove_original_avatar_from_world(
    const odh::LiveOriginalRecoveryPlacementSource&,SessionObjectPositionContext,
    const OriginalAvatarRemovalServices&);
}
