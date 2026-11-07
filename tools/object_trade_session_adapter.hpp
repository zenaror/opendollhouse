// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/object_trade_transaction.hpp"
#include "odh/prepared_architecture.hpp"
#include "odh/room_map.hpp"
#include "odh/saved_architecture.hpp"
#include "odh/tuning.hpp"
#include "odh/architecture_object_support.hpp"
#include "linux_family_session_journal.hpp"
#include <memory>

namespace odh_host {
// Source SetIndivPosition's null destination is LotTilePos.OUT_OF_WORLD,
// whose level is explicitly1. This actual new source publication proves that
// scalar independently from unknown hierarchy backing, which stays unknown.
// Imports without this source operation acquire no inferred native scalar.
// Caller owns direction's source/group write and its lifecycle phases.
// Contained descendants receive the parent's full pose in the actual Post.
bool publish_source_individual_position(odh::World&,odh::WorldEntityReference,
    const std::optional<odh::EntityPosition>&,std::uint8_t direction,
    const std::function<bool()>& source_current);
enum class SessionPositionPhase {pre,publish,post};
using SessionSlotTransferPhase=SessionPositionPhase;
struct SessionSlotTransferResult {
    bool completed=false,placed=false;
    std::string diagnostic;
};
struct ObjectTradeLoadedResource {
    odh::ObjectTradeResource code; // Global bank supplied by command_resource().
    odh::ObjectTradeCatalogItem catalog;
    std::vector<std::int16_t> definition_words;
    odh::TuningOwner tuning;
};
// Reads original files only. Definition ambiguity is never archive precedence.
// Preload before entering a candidate/VM callback; find() never performs I/O.
class ObjectTradeResourceLibrary {
public:
    ObjectTradeResourceLibrary(std::filesystem::path game_root,const odh::DefinitionIndex&,
        std::filesystem::path global_archive,std::string global_entry);
    const ObjectTradeLoadedResource& load(std::uint32_t guid);
    // A cached immutable record is readable during dispatch. First admission
    // requires a dispatch-free boundary; the actual VM proves that state.
    const ObjectTradeLoadedResource& creation_resource(std::uint32_t guid,const odh::Vm* source_vm);
    void preload(const odh::WorldSnapshot&);
    const ObjectTradeLoadedResource* find(std::uint32_t guid) const noexcept;
    odh::ObjectTradeResource command_resource(std::uint32_t guid);
    // Owned complete construction plan, preserving exact IFF part order and
    // requested catalog identity. No World allocation or Init occurs here.
    odh::ObjectTradeResource load_group(std::uint32_t requested_guid);
    const odh::DefinitionIndex& definitions() const {return definitions_;}
    const std::shared_ptr<const odh::VmProgram>& global_program() const {return globals_;}
    const odh::TuningTables& global_tuning() const {return global_tuning_;}
private:
    std::filesystem::path root_,global_archive_;
    std::string global_entry_;
    const odh::DefinitionIndex& definitions_;
    std::shared_ptr<const odh::VmProgram> globals_;
    odh::TuningTables global_tuning_;
    std::map<std::uint32_t,ObjectTradeLoadedResource> objects_;
    std::size_t instructions_=0;
};

// One bounded allocation plan for household and actorless transactions. It
// observes the actual live cohort and reserved IDs; retired reusable numbers
// remain available according to the supplied history, not guessed from saves.
std::vector<std::int16_t> allocate_object_trade_group_ids(const odh::WorldSnapshot&,
    const odh::SessionObjectHistory&,std::size_t part_count);

// Shared live/candidate ordinary-object placement. Caller provides the actual
// World and actual threads, not a fabricated opcode42/45 creation capability.
// All pointers, bank references and callbacks remain valid until the returned
// services are destroyed. Geometry is the exact current prepared lot, not a
// guessed empty room. Registry/query state is private to this binding.
struct SessionObjectPositionContext {
    odh::World* world=nullptr;
    ObjectTradeResourceLibrary* resources=nullptr;
    const odh::PreparedArchitecture* prepared=nullptr;
    const odh::SavedArchitecture* architecture=nullptr;
    const odh::GeometricRoomMap* rooms=nullptr;
    // Event-owned support state from the same live/candidate session. Null
    // means unknown legacy history and must not become an empty grid.
    odh::ArchitectureObjectSupport* object_support=nullptr;
    // Present-but-incomplete is a failed authority, not legacy absence: event
    // callbacks refuse it. Candidate transactions preserve complete ledgers via
    // exact Pre/publish/Post guards and verify cohort coherence before publish.
    odh::ObjectPositionRegistrySnapshot* position_registry=nullptr;
    std::vector<odh::ObjectTradeGroup> groups;
    std::function<odh::Vm*(std::int16_t)> thread_for;
    std::function<std::optional<odh::CodeOwner>(std::int16_t)> owner_for;
    odh::VmMemory memory;
    std::function<odh::VmPrimitive(const odh::VmMemory&)> primitive_factory;
    std::function<bool()> source_still_matches;
    // Fact from VMScheduler.RunningNow: true inside a real tick, false for UI
    // commands. Only the actual register_owner's Temps/XL may be shared.
    bool share_check_registers=false;
    // Native recovery's EP9 uses its receiver's Thread/TEMP defaults. Bound by
    // that scoped host only; ordinary placement retains its historical profile.
    std::function<bool(odh::ObjectPositionIdentity)> native_entry_point9;
    // Host observes only the exact phase's source-owned mutations. The fixed
    // publisher is minted by the shared transfer kernel, not an arbitrary VM
    // callback; Init, placement checks and unrelated mutations remain outside.
    std::function<bool(odh::ObjectPositionIdentity,odh::ObjectPositionIdentity,std::size_t,
        SessionSlotTransferPhase,const std::function<bool()>&)> slot_phase;
    // Floor/OOW has no container or slot. The exact optional destination and
    // real source authority bound by this context delimit each owned phase.
    // Publishing direction is explicit even for OOW (no position carries it).
    // Pre/Post pass null; no direction capability is inferred from live writes.
    std::function<bool(odh::ObjectPositionIdentity,const std::optional<odh::EntityPosition>&,
        SessionPositionPhase,std::optional<std::uint8_t>,const std::function<bool()>&)> position_phase;
    // Exact retired-container lease may supply source Pre detach after parent
    // removal. Absent for normal live placement. Never a numeric-ID fallback.
    std::function<bool(odh::ObjectPositionIdentity,odh::ObjectPositionIdentity,std::size_t)> detach_container;
    std::optional<odh::World::RetiredContainer> retired_container;
    // Actual source field captured before the retirement owner releases its
    // live constructor metadata. Unknown stays unknown, never defaultfalse.
    std::optional<bool> retired_parent_moved_self;
    // Exact live physical-group authority; absent preserves the legacy path.
    std::function<std::optional<odh::ObjectGroup>(odh::ObjectPositionIdentity)> group;
    std::function<std::optional<bool>(odh::ObjectPositionIdentity)> admit_group_member;
    // Imported physical groups have no fabricated common master. This exact
    // source-manifest proof is required only for definition_guid0.
    std::function<bool(const odh::ObjectGroup&)> authenticate_imported_group;
    // Registration needs the complete identity cohort, not movement offsets or
    // price metadata. Candidate bindings retain that separately so an unknown
    // legacy price does not erase known source door/window membership.
    std::function<std::optional<std::vector<odh::ObjectPositionIdentity>>(odh::ObjectPositionIdentity)> registration_group;
    std::function<bool(const odh::ObjectGroup&)> authenticate_live_group;
    std::function<bool(odh::ObjectPositionIdentity)> dynamic_group_update;
    std::function<std::optional<std::vector<odh::ObjectPositionIdentity>>(odh::ObjectPositionIdentity)> split_dynamic_group;
    std::function<bool(odh::ObjectPositionIdentity)> update_dynamic_group_flags;
    std::function<std::optional<odh::NativeObjectGroupRotation>(odh::ObjectPositionIdentity,std::uint8_t)> begin_native_rotation;
    std::function<bool(const odh::NativeObjectGroupRotation&)> rollback_native_rotation;
    std::function<std::optional<odh::NativeObjectGroupOffset>(odh::WorldEntityReference)> native_current_offset;
    std::function<odh::NativeObjectGroupLink(odh::WorldEntityReference)> native_group_head,native_group_next;
    // Optional shared transaction budget and authenticated full owner banks.
    std::uint64_t* instruction_budget=nullptr;
    std::function<odh::SharedRoutineOwners(std::int16_t)> owner_programs;
};
struct SessionObjectPositionServices {
    odh::ObjectPositionValidServices placement;
    odh::ObjectPositionChangeServices change;
    std::function<bool(odh::ObjectPositionIdentity,bool)> record_moved_self,static_footprint_changed;
    std::function<bool(odh::ObjectPositionIdentity)> update_footprint;
    // Synchronous resource-owned entry execution shared by source load replay.
    std::function<std::optional<bool>(odh::ObjectPositionIdentity,std::uint16_t,std::int16_t,
        const std::vector<std::int16_t>&)> execute_entry;
    std::function<bool(odh::ObjectPositionIdentity,bool)> before_slot_transfer;
    std::function<bool(odh::ObjectPositionIdentity,bool)> after_slot_transfer;
    std::function<std::string()> lifecycle_diagnostic;
    std::function<SessionSlotTransferResult(odh::ObjectPositionIdentity,odh::ObjectPositionIdentity,std::size_t)> transfer_to_slot;
};
SessionObjectPositionServices make_session_object_position_services(SessionObjectPositionContext);

struct ObjectTradeThreadPrograms {
    odh::CodeOwner primary_owner=0;
    std::shared_ptr<const odh::VmProgram> globals;
    odh::SharedRoutineOwners owners;
    // Exact load admission policy of an authenticated imported source object Thread.
    // Residents, active commands and newly constructed Threads remain strict.
    odh::VmSavedRoutinePolicy saved_routine_policy=odh::VmSavedRoutinePolicy::require_present;
};
struct ObjectTradeCandidateContext {
    odh::World& world;
    odh::RelationshipState& relationships;
    odh::SessionHostState& host;
    odh::RandomState& random;
    odh::RuntimeGlobals* globals;
    const odh::Ts1Clock* clock;
    ObjectTradeResourceLibrary& resources;
    std::function<odh::Vm*(std::int16_t)> thread_for;
};
struct ObjectTradeSessionHooks {
    odh::NativeObjectGroupConstructionProfile native_group_construction=odh::NativeObjectGroupConstructionProfile::unknown_legacy;
    // External safe point only: complete codec/loader-validated live snapshot.
    std::function<odh::SessionBundle()> capture_current;
    // Native Household/SingleResidentSessionRestore::prepare, before any I/O.
    std::function<void(const odh::SessionBundle&,const std::vector<std::uint8_t>&)> validate_reopen;
    // Exact immutable banks from the threads represented by capture_current.
    // Adapter rebuilds private register owners; it never borrows live mutable VMs.
    std::function<ObjectTradeThreadPrograms(std::int16_t,const odh::SessionBundle&)> thread_programs;
    // Remaining native services beyond built-in expressions, RNG, Budget,
    // ObjectType and SetToNext. Must bind ONLY supplied candidate state/memory.
    // No publication, I/O, retained live references or manufactured success.
    std::function<odh::VmPrimitive(const ObjectTradeCandidateContext&,const odh::VmMemory&)> extra_primitive_factory;
    std::function<std::vector<odh::ObjectTradeGroup>()> physical_groups;
    // Pure authenticated Work.iff lookup. Candidate VMs pass their own frame
    // and Temp0/Temp1 selectors; never capture/read live mutable Thread banks.
    std::function<std::optional<std::int16_t>(const odh::VmFrame&,std::uint16_t,
        std::int16_t,std::int16_t)> job_data;
    // Complete source physical membership/base/offset authority. Ordinary
    // singleton legacy callers may omit it; imported multipart commands may
    // not reconstruct this information from mutable price provenance.
    std::function<std::optional<odh::ObjectGroup>(odh::ObjectPositionIdentity)> physical_group;
};
// Resolve existing physical source membership for both household and frozen
// lot commands, independently of mutable price-origin metadata. I/O stays in
// this external preparation stage, never inside a candidate VM callback.
odh::ObjectTradeResource resolve_object_trade_source_group(ObjectTradeResourceLibrary&,
    const odh::SessionBundle&,const ObjectTradeSessionHooks&,const std::vector<odh::ObjectTradeGroup>&,
    odh::ObjectTradeIdentity);

// Real candidate binding shared by household transactions and actorless edits.
// Source/resources/geometry/hooks must outlive the synchronous preparation.
odh::ObjectTradeBindingFactory make_object_trade_candidate_factory(ObjectTradeResourceLibrary&,
    const odh::SessionBundle& component_state,const odh::PreparedArchitecture&,
    const odh::SavedArchitecture&,const odh::GeometricRoomMap&,const ObjectTradeSessionHooks&,
    const std::vector<odh::ObjectTradeGroup>&,std::int16_t selected,odh::CodeOwner owner,std::uint32_t guid,
    std::function<bool()> authority,odh::ArchitectureObjectSupport*,odh::ObjectPositionRegistrySnapshot*);

enum class ObjectTradeSessionCommandKind { purchase,move,sell,cancel };
struct ObjectTradeSessionCommand {
    ObjectTradeSessionCommandKind kind=ObjectTradeSessionCommandKind::cancel;
    std::uint32_t guid=0; // Purchase: exact definition, price is read from OBJD.
    odh::ObjectTradeIdentity target; // Existing object commands, with generation.
    odh::EntityPosition position{}; // Includes requested rotation; no preview writes.
    bool move_objects=false; // Explicit source cheat, never an implicit bypass.
};
enum class ObjectTradeSessionState { cancelled,rejected,unsupported,published_reload_required };
struct ObjectTradeSessionResult {
    ObjectTradeSessionState state=ObjectTradeSessionState::unsupported;
    odh::ObjectTradeTransactionStatus transaction=odh::ObjectTradeTransactionStatus::unsupported;
    std::string diagnostic;
    std::filesystem::path checkpoint;
    std::vector<std::uint8_t> archive;
    bool durability_uncertain=false;
    // Publication is durable/logically committed before this result. The host
    // must reopen THIS checkpoint in the same house and must not resume its old
    // World or replace the new budget with a later stale save-on-exit capture.
};
class ObjectTradeSessionAdapter {
public:
    ObjectTradeSessionAdapter(ObjectTradeResourceLibrary&,FamilySessionJournal&,
        const odh::PreparedArchitecture&,const odh::SavedArchitecture&,
        const odh::GeometricRoomMap&,ObjectTradeSessionHooks);
    ObjectTradeSessionResult execute(const ObjectTradeSessionCommand&);
private:
    ObjectTradeResourceLibrary& resources_;FamilySessionJournal& journal_;
    const odh::PreparedArchitecture& prepared_;const odh::SavedArchitecture& architecture_;
    const odh::GeometricRoomMap& rooms_;ObjectTradeSessionHooks hooks_;bool executing_=false;
};
} // namespace odh_host
