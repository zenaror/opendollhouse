#pragma once

#include "odh/entity_scheduler_snapshot_codec.hpp"
#include "odh/entity_thread_controllers_snapshot_codec.hpp"
#include "odh/relationship_snapshot_codec.hpp"
#include "odh/session_archive.hpp"
#include "odh/session_resource_identity.hpp"
#include "odh/playback.hpp"
#include "odh/routing.hpp"
#include "odh/sleep.hpp"
#include "odh/ts1_simulation.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/world_snapshot_codec.hpp"

#include <optional>
#include <functional>

namespace odh {

struct SessionGlobalValue {
    std::uint16_t index = 0;
    std::int16_t value = 0;
};

struct SessionHostState {
    std::uint8_t neighborhood_mode = 0;
    std::uint16_t house_number = 0;
    std::uint16_t family_chunk_id = 0;
    std::int32_t family_number = 0;
    std::uint32_t resident_guid = 0;
    std::int16_t resident_neighbor_id = 0;
    std::int16_t runtime_avatar_id = 0;
    std::uint64_t tick = 0;
    std::uint64_t random_seed = 0;
    std::optional<std::int32_t> family_budget;
    std::vector<SessionGlobalValue> global_values; // Strictly ascending indices.
};

// Provenance for a semiglobal code bank linked by the exact object's GLOB.
// archive is canonical and relative to the selected game root; entry is the
// exact FAR entry selected from that archive.
struct SessionLinkedSemiglobal {
    std::string name, archive, entry;
    bool operator==(const SessionLinkedSemiglobal& other) const {
        return name==other.name&&archive==other.archive&&entry==other.entry;
    }
    bool operator!=(const SessionLinkedSemiglobal& other) const { return !(*this==other); }
};

struct SessionDynamicActionOwner {
    CodeOwner owner = 0;
    std::int16_t target_id = 0;
    std::uint64_t target_generation = 0;
    std::uint32_t target_guid = 0;
    std::string source, asset;
    std::uint16_t definition_chunk_id = 0, tree_table_id = 0, root_routine = 0;
    std::optional<SessionLinkedSemiglobal> linked_semiglobal;
    std::array<std::uint8_t,32> routine_fingerprint{};
};

// Resource ownership is independent of the objects currently named by a call.
// The host rebuilds this exact bank before applying any VM snapshot.
enum class SessionCodeBankKind : std::uint8_t {
    private_closure = 0, complete_private = 1, private_and_semiglobal = 2
};
enum class SessionCodeOwnerIdentity : std::uint8_t { process = 0, resource_guid = 1 };
struct SessionResourceCodeOwner {
    CodeOwner owner = 0;
    std::uint32_t resource_guid = 0;
    std::string source, asset;
    std::uint16_t definition_chunk_id = 0, tree_table_id = 0, root_routine = 0;
    SessionCodeBankKind bank_kind = SessionCodeBankKind::private_closure;
    SessionCodeOwnerIdentity identity = SessionCodeOwnerIdentity::process;
    std::optional<SessionLinkedSemiglobal> linked_semiglobal;
    std::array<std::uint8_t,32> routine_fingerprint{};
};
struct SessionCreatedObject {
    std::int16_t id = 0;
    std::uint64_t generation = 0;
    std::uint32_t guid = 0;
    std::string source, asset;
    std::uint16_t definition_chunk_id = 0;
};
struct SessionRemovedOriginalObject {
    std::int16_t id = 0;
    std::uint64_t generation = 0;
    std::uint32_t guid = 0;
};
struct SessionDeferredRemoval {
    std::int16_t id = 0;
    std::uint64_t generation = 0;
};
using SessionCreatedObjectResolver = std::function<Entity(const SessionCreatedObject&)>;

struct SessionRuntimeEntity {
    std::int16_t id = 0;
    VmSnapshot vm;
    EntityThreadControllersSnapshot controllers;
    PlaybackSnapshot playback;
    std::optional<LocalRouteSnapshot> route;
    std::vector<SessionDynamicActionOwner> dynamic_action_owners; // Strictly ascending owner IDs.
    std::vector<SessionResourceCodeOwner> resource_owners; // Installed generic code banks, including idle cached owners; ascending IDs.
};

// A process-local owner is meaningful only with its exact resource provenance
// and the digest of the bounded private-call closure loaded from that resource.
struct SessionActionCodeOwner {
    CodeOwner owner = 0;
    std::string source, asset;
    std::uint16_t root_routine = 4097;
    std::array<std::uint8_t,32> routine_fingerprint{};
};

// Value-only continuation for a non-avatar object Main thread. The host must
// rebuild this object's exact private/linked code owners and bind a fresh VM
// and Sleep controller before applying these values. It is deliberately kept
// separate from SessionRuntimeEntity: objects do not own avatar interaction
// queues or Playback state.
struct SessionObjectThread {
    std::int16_t id = 0;
    std::uint64_t generation = 0;
    std::uint32_t guid = 0;
    std::vector<SessionActionCodeOwner> code_owners; // Strictly ascending owner IDs.
    VmSnapshot vm;
    SleepSnapshot sleep;
    std::vector<SessionResourceCodeOwner> resource_owners; // Installed generic code banks, including idle cached owners; ascending IDs.
    CodeOwner primary_owner = 0; // Explicit object private bank; zero only for legacy OBVM.
};

// The seated continuation describes only the stable, completed Sit -> Stand
// boundary. It cannot represent an in-flight interaction, route or portal.
struct SessionSeatedContinuation {
    std::int16_t actor_id = 0, target_id = 0;
    std::uint64_t actor_generation = 0, target_generation = 0;
    std::uint32_t target_guid = 0, target_slot = 0;
    std::uint16_t action = 4097;
    InteractionCompletion completion{};
    std::string source, asset;
    std::uint16_t definition_chunk_id = 0, tree_table_id = 0;
    std::uint16_t stand_entry_point = 27, stand_condition = 0, stand_action = 4102;
    std::optional<SessionLinkedSemiglobal> linked_semiglobal;
    std::array<std::uint8_t,32> sit_routine_fingerprint{}, stand_routine_fingerprint{};
    std::vector<SessionActionCodeOwner> code_owners; // Strictly ascending owner IDs.
};

// HOST v2 requires this complete, portable HHRD v1 roster. HOST v1 is legacy.
struct SessionHouseholdMember {
    std::int16_t id = 0;
    std::uint64_t generation = 0;
    std::uint32_t guid = 0;
    std::int16_t neighbor_id = 0;
    std::uint16_t object_definition_id = 0;
    // Stable catalog identity: UserData/Characters/... or PlayerData/Characters/...
    std::string character_asset;
    std::array<std::uint8_t,32> character_fingerprint{};
};
bool same_session_household_roster(const std::vector<SessionHouseholdMember>&,
    const std::vector<SessionHouseholdMember>&);

struct SessionAvatarMotiveState {
    std::int16_t id=0;
    Ts1MotiveSnapshot decay;
};
struct SessionSimulationState {
    Ts1ClockSnapshot clock;
    std::vector<SessionAvatarMotiveState> avatars; // Strictly ascending runtime IDs.
};

// Immutable family/source identity plus the active occupancy generation.
struct SessionFamilyLease {
    std::array<std::uint8_t,32> neighborhood_identity{};
    std::array<std::uint8_t,32> family_identity{};
    std::uint64_t occupancy_epoch = 0;
    std::uint16_t family_chunk_id = 0;
};

// Optional presentation selection; it grants no simulation/family authority.
struct SessionUiState {
    std::int16_t active_actor = 0;
    std::uint64_t generation = 0;
    std::uint32_t guid = 0;
};

// Values only. The host must rebuild matching resources, programs, callbacks,
// and presentation state before applying these snapshots to a fresh session.
struct SessionBundle {
    std::optional<SessionSimulationState> simulation; // Absent in older archives.
    SessionResourceIdentity resources;
    SessionHostState host;
    std::vector<SessionHouseholdMember> household_roster; // Empty only for legacy HOST v1.
    WorldSnapshot world;
    RelationshipSnapshot relationships;
    std::vector<SessionRuntimeEntity> runtime_entities; // Strictly ascending IDs.
    std::vector<SessionObjectThread> object_threads; // Strictly ascending IDs, disjoint from avatars.
    EntityVmSchedulerSnapshot scheduler;
    std::optional<SessionSeatedContinuation> seated_continuation; // Legacy HOST v1 / SEAT v1-v2.
    // HOST v2 / required SEAT v3; ascending actor IDs, only seated residents.
    // Mutually exclusive with the legacy singular continuation.
    std::vector<SessionSeatedContinuation> household_seated_continuations;
    // GEXP v1 is required when true: older readers must reject this profile.
    bool generic_execution = false;
    CodeOwner next_code_owner = 0x80000000U;
    bool code_owner_ids_exhausted = false;
    std::vector<SessionCreatedObject> created_objects; // Ascending live IDs.
    std::vector<SessionRemovedOriginalObject> removed_original_objects; // Ascending baseline IDs.
    std::vector<SessionDeferredRemoval> deferred_removals; // Ordered host intents, unique identities.
    // Required FAML v1 when present. The overlay owns the checkpoint head;
    // this identifies the occupancy, not permission to overwrite a newer save.
    std::optional<SessionFamilyLease> family_lease;
    // Optional UIST v1; absent archives retain the host's legacy selection fallback.
    std::optional<SessionUiState> ui_state;
};

struct SessionBundleLimits {
    SessionArchiveLimits archive{};
    // The saved-lot host uses a 131,072-word budget for House02 and larger lots.
    WorldSnapshotCodecLimits world{{4096,131072,4096},64u*1024u*1024u};
    RelationshipSnapshotCodecLimits relationships{};
    VmSnapshotCodecLimits vm{};
    EntityVmSchedulerSnapshotCodecLimits scheduler{};
    EntityThreadControllersSnapshotCodecLimits controllers{};
    std::size_t max_playback_events = 4096;
    std::size_t max_playback_queue = 4096;
    std::uint32_t max_playback_frames = 1048576;
    std::uint64_t max_playback_ticks = 1048576;
    std::size_t max_runtime_entities = 64;
    std::size_t max_route_points = 262145;
    std::size_t max_object_threads = 64;
    std::size_t max_resource_owners_per_thread = 256;
    std::size_t max_host_globals = 256;
};

// Shared value-only validation for a host continuation profile that accepts
// an already-serialized Main animation wait. Does not modify execution state.
void validate_session_playback_links(const SessionRuntimeEntity&, const Entity&,
    const SessionHostState&, const SessionBundleLimits& = {});

std::vector<std::uint8_t> encode_session_bundle(
    const SessionBundle& bundle, SessionBundleLimits limits = {});
SessionBundle decode_session_bundle(
    const std::vector<std::uint8_t>& bytes,
    std::optional<SessionResourceIdentity> expected_resources = std::nullopt,
    SessionBundleLimits limits = {});

} // namespace odh
