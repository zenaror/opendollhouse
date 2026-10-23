#pragma once

#include "odh/entity_scheduler_snapshot_codec.hpp"
#include "odh/entity_thread_controllers_snapshot_codec.hpp"
#include "odh/relationship_snapshot_codec.hpp"
#include "odh/session_archive.hpp"
#include "odh/session_resource_identity.hpp"
#include "odh/vm_snapshot_codec.hpp"
#include "odh/world_snapshot_codec.hpp"

#include <optional>

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
    std::vector<SessionGlobalValue> global_values; // Strictly ascending indices.
};

struct SessionRuntimeEntity {
    std::int16_t id = 0;
    VmSnapshot vm;
    EntityThreadControllersSnapshot controllers;
};

// Values only. The host must rebuild matching resources, programs, callbacks,
// and presentation state before applying these snapshots to a fresh session.
struct SessionBundle {
    SessionResourceIdentity resources;
    SessionHostState host;
    WorldSnapshot world;
    RelationshipSnapshot relationships;
    std::vector<SessionRuntimeEntity> runtime_entities; // Strictly ascending IDs.
    EntityVmSchedulerSnapshot scheduler;
};

struct SessionBundleLimits {
    SessionArchiveLimits archive{};
    // The saved-lot host uses a 131,072-word budget for House02 and larger lots.
    WorldSnapshotCodecLimits world{{4096,131072,4096},64u*1024u*1024u};
    RelationshipSnapshotCodecLimits relationships{};
    VmSnapshotCodecLimits vm{};
    EntityVmSchedulerSnapshotCodecLimits scheduler{};
    EntityThreadControllersSnapshotCodecLimits controllers{};
    std::size_t max_runtime_entities = 64;
    std::size_t max_host_globals = 256;
};

std::vector<std::uint8_t> encode_session_bundle(
    const SessionBundle& bundle, SessionBundleLimits limits = {});
SessionBundle decode_session_bundle(
    const std::vector<std::uint8_t>& bytes,
    std::optional<SessionResourceIdentity> expected_resources = std::nullopt,
    SessionBundleLimits limits = {});

} // namespace odh
