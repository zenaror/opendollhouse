// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "object_trade_session_adapter.hpp"
#include "odh/saved_object_recovery.hpp"
#include "odh/object_group.hpp"
#include <map>
#include <set>

namespace odh_host {
// Native 422c62 uses the CURRENT world-oriented +1a0/+1a4/+1a8 fields,
// distinct from an unrotated construction plan. XY are VM units (16/tile).
struct SavedRecoveryNativeOffset {std::int32_t x=0,y=0,level=0;};
// Native422055 expands current native tile XY with SHL4 before422c62 uses
// them. Convert at the binding boundary; adapter consumers already use World
// units. Negative offsets are valid; level is an unscaled floor delta.
std::optional<SavedRecoveryNativeOffset> saved_recovery_offset_from_native_tiles(
    const odh::NativeObjectGroupOffset&,std::string& error);
struct SavedRecoveryObjectContext {
    SessionObjectPositionContext position;
    // Construct with make_session_object_position_services(position), retaining
    // its actual register owners, indices and publication guards. Tests can
    // bind those same services to fixture-owned World/geometry/registry.
    SessionObjectPositionServices services;
    const odh::DefinitionIndex* definitions=nullptr;
    // These optional actual architecture owners override the generic session
    // services. Nonzero exclusive occupied walls cannot use their restricted
    // fallback. Architectural style publication is mandatory for doors/windows.
    std::function<bool(odh::ObjectPositionIdentity,bool,bool)> wall_use;
    std::function<bool(odh::ObjectPositionIdentity)> architectural_wall_style;
    // Actual use/queue owner: clear source UseCount (verified ObjectData62). A raw bank write is
    // insufficient when the application derives source UseCount from users.
    std::function<bool(odh::WorldEntityReference,std::string&)> clear_use_count;
    // Native422d79 with NULL arguments: the exact receiver's actual Thread
    // supplies TEMP defaults using the current routine's parameter metadata.
    // True means synchronous completion (including BHAV false), not its return
    // value. No explicit-zero-argument or generic execute_entry fallback.
    std::function<bool(odh::WorldEntityReference,std::uint16_t,std::string&)> native_entry;
    // Shared native4331d4 physical consumer, including old-parent EP9. Generic
    // Pre/null/Post and EP9 on the removed body do not satisfy this contract.
    std::function<bool(odh::WorldEntityReference,std::string&)> native_detach;
    // Native432a70 hierarchy owners used inside restoration's Pre/Position.
    // Unlink does not execute EP10 or publish scalar OOW. Restore inserts the
    // displaced destination occupant under native0 and publishes actual XYZ.
    std::function<bool(odh::WorldEntityReference,std::string&)> native_unlink;
    std::function<bool(odh::WorldEntityReference,std::optional<odh::EntityPosition>,
        std::optional<odh::WorldEntityReference>,std::size_t,std::optional<odh::EntityPosition>,std::string&)> native_restore_position;
    // F2 presentation event at the END of native3333ef, after its own EP9s
    // and before the separate native MT422c62 placement-entry pass.
    std::function<bool(odh::WorldEntityReference,std::string&)> native_position_finished;
    //3333ef seating notifications: false=old front after unlink; true=new
    // center unconditionally, then new front if current Sit26 exists.
    std::function<bool(odh::WorldEntityReference,const odh::EntityPosition&,bool,std::string&)> native_position_neighbors;
    // Actual live group owners, never a saved/frozen member-list fallback.
    std::function<std::optional<SavedRecoveryNativeOffset>(odh::WorldEntityReference)> native_offset;
    std::function<bool(odh::WorldEntityReference leader,odh::WorldEntityReference target,std::string&)> restore_group;
};
// One original-import activation. Operates on the SAME candidate World and its
// real event ledgers. Main/restart, Init/Load and person handling remain in the
// caller's coordinator. No PositionValid probe, object recreation or second VM.
class SavedRecoveryObjectAdapter {
public:
    explicit SavedRecoveryObjectAdapter(SavedRecoveryObjectContext);
    SavedRecoveryObjectAdapter(const SavedRecoveryObjectAdapter&)=delete;
    SavedRecoveryObjectAdapter& operator=(const SavedRecoveryObjectAdapter&)=delete;
    SavedRecoveryObjectAdapter(SavedRecoveryObjectAdapter&&)=delete;
    SavedRecoveryObjectAdapter& operator=(SavedRecoveryObjectAdapter&&)=delete;
    static bool handles(odh::SavedRecoveryPhase);
    bool preflight(const std::vector<odh::SavedRecoveryEntity>&,std::string&);
    bool supports(const odh::SavedRecoveryCall&,std::string&) const;
    bool execute(const odh::SavedRecoveryCall&,std::string&);
private:
    using Ref=odh::WorldEntityReference;
    struct Snapshot {
        std::optional<odh::EntityPosition> position;
        std::optional<odh::EntityPosition> native_position;
        std::optional<Ref> parent;
        std::size_t slot=0;
    };
    struct PortalState {std::vector<Ref> order;std::set<Ref> members;std::set<std::pair<std::uint16_t,std::int16_t>> rooms;bool detached=false;};
    SavedRecoveryObjectContext context_;
    std::map<Ref,odh::SavedRecoveryEntity> profiles_;
    std::map<Ref,Snapshot> snapshots_;
    std::map<Ref,PortalState> portals_;
    bool current()const;
    const odh::Entity* entity(Ref)const;
    std::optional<bool> admit(odh::ObjectPositionIdentity)const;
    std::optional<std::vector<odh::ObjectPositionIdentity>> group(Ref)const;
    bool entry(Ref,std::uint16_t,std::string&);
    bool pre(Ref,std::string&);
    bool post(Ref,std::string&);
    bool publish(Ref,const std::optional<odh::EntityPosition>&,std::uint8_t,std::string&);
    bool portal(Ref,odh::SavedRecoveryPhase,std::string&);
    bool regenerate(std::uint16_t,std::int16_t,std::string&);
};
} // namespace odh_host
