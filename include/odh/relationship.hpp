#pragma once
#include "odh/neighbor_context.hpp"
#include "odh/world.hpp"

namespace odh {
enum class RelationshipMatrix { neighbor, object };
struct RelationshipLimits {
    std::size_t sources = 4096, pairs = 16384, values = 65536;
};
struct RelationshipPairSnapshot {
    std::int32_t target;
    std::vector<std::int16_t> values;
};
struct RelationshipSourceSnapshot {
    RelationshipMatrix matrix;
    std::int16_t id;
    std::uint64_t generation;
    std::vector<RelationshipPairSnapshot> pairs;
};
// Owned runtime values, including empty sources/pairs and obsolete object
// generations. No World, resources, callbacks or session file format is implied.
struct RelationshipSnapshot {
    RelationshipLimits limits;
    std::vector<RelationshipSourceSnapshot> sources;
    std::size_t pair_count = 0, value_count = 0;
};
struct SavedObjectRelationship {
    std::int32_t target;
    std::vector<std::int32_t> values;
};
struct SavedObjectRelationships {
    std::int16_t source;
    std::vector<SavedObjectRelationship> relationships;
};
struct RelationshipRecord {
    VmContext context;
    RelationshipMatrix matrix = RelationshipMatrix::neighbor;
    std::int32_t source = 0, target = 0;
    std::uint8_t index = 0, mode = 0, set_mode = 0;
    std::optional<std::int16_t> value_before, value_after;
    bool created = false, grown = false, success = false;
    bool skipped_entities = false;
    float diff_multiplier = 1; // Explicit default; no external category tuning supplied.
};
// Owns a mutable TS1 matrix copied from the immutable neighbor snapshot.
// Disk int32 values become signed low16 words; raw int32 target keys and tails
// beyond byte-addressable index255 remain owned and preserved. Object matrices
// are keyed by source identity/generation and target ushort ID;
// no TSO persistence, TS1 save writer or inbound relationship index is implied.
class RelationshipState {
public:
    explicit RelationshipState(RelationshipLimits limits = {}): limits_(limits) {}
    explicit RelationshipState(const NeighborContext&, RelationshipLimits = {});
    RelationshipState(const RelationshipState&) = delete;
    RelationshipState& operator=(const RelationshipState&) = delete;
    RelationshipState(RelationshipState&&) = delete;
    RelationshipState& operator=(RelationshipState&&) = delete;
    RelationshipSnapshot snapshot() const;
    // Read only the selected neighbor's directional pairs for a UI panel.
    // No matrix mutation or implicit zero-valued relationship is introduced.
    std::vector<RelationshipPairSnapshot> neighbor_pairs(std::int16_t source) const;
    // Fresh destination only, including for an empty snapshot. Saved limits
    // must fit the destination's configured caps and become its runtime limits.
    // Invalid state returns false; allocation exceptions propagate. Both leave
    // the destination unchanged. No World or callback binding is restored.
    bool restore_snapshot(const RelationshipSnapshot&);
    // An initialized owner can acquire neighbors added to the authenticated
    // current catalog after its lot snapshot was captured. Add ONLY missing
    // NBRS sources, including known-empty records; preserve every existing
    // neighbor/object source and value. Never manufacture an unknown ID in a
    // primitive. Caller supplies the effective catalog after logical deletions.
    // Duplicate new targets, combined budget overflow or an uninitialized owner
    // reject without mutation; allocation exceptions preserve the old snapshot.
    bool admit_missing_neighbor_sources(const NeighborContext&);
    // Atomically bind saved OBJECT sources to their actual World generations.
    // Empty relations bind/count a source; empty batches succeed. Existing bound
    // generations, missing/duplicate sources, duplicate low16 target keys and
    // combined NBRS/OBJECT budget overflow return false without mutation.
    // Targets need not exist in World. Values become signed low16 words, with
    // no clamp and all tails preserved; caller retains the raw saved snapshot.
    // Allocation exceptions propagate with all matrix/counts unchanged.
    bool import_object_relationships(const World&, const std::vector<SavedObjectRelationships>&);
    // Retire one OBJECT source identity and remove inbound OBJECT pairs whose
    // ushort target is id. Neighbor matrices and other source identities are
    // retained; inbound removal also applies to those other OBJECT identities.
    // The host validates the live World generation first. Valid repeated calls
    // are idempotent; invalid IDs
    // return false without mutation. Performs no allocation or I/O.
    bool retire_object_identity(std::int16_t id, std::uint64_t generation) noexcept;
    // Remove a deleted Sim's neighbor source and inbound neighbor pairs only.
    // Object matrices and unrelated neighbor values survive. Idempotent, no I/O.
    bool retire_neighbor_identity(std::int16_t id) noexcept;
    // Actual Delete preserves outgoing pairs to OTHER bodies. Indexed sources
    // and the exact retiring source lose the numeric inbound pair (including
    // self-target); other retained bodies stay untouched.
    bool remove_inbound_object_references(const World&,std::int16_t id,std::uint64_t generation) noexcept;
    // GC removes only this collected old source, never inbound pairs of a
    // replacement live object that happens to have reused its numeric ID.
    bool forget_object_source(std::int16_t id,std::uint64_t generation) noexcept;
    // State and World must outlive the returned callback/bound VM. Captured
    // memory/observer callbacks are owned. Callbacks must not reenter or mutate
    // this relationship state; observers must not throw;
    // memory.write returning false must leave its backing store unchanged.
    // Validation, budgets and allocations precede writes/publication; allocation
    // exceptions propagate with the matrix unchanged. Creation temporarily
    // copies the bounded matrix; existing pair growth/writes copy only its list.
    // Existing reads/no-ops do not copy relationship storage. Old object generations
    // remain counted and bounded until this state is discarded.
    VmPrimitive primitive(const World&, VmMemory = {},
        std::function<void(const RelationshipRecord&)> observer = {});
    std::optional<std::int16_t> value(std::int16_t source, std::int32_t target, std::size_t index) const;
    std::optional<std::int16_t> object_value(const World&, std::int16_t source,
        std::int16_t target, std::size_t index) const;
    // Pinned FreeSO7e76 scope24 field5. Count reciprocal neighbor score0>=50
    // against this mutable matrix, not immutable NBRS. Raw target keys narrow
    // to signed low16 neighbor IDs. Empty accessed score vectors are unknown
    // (the reference throws); no pairs, values or source entries are created.
    std::optional<std::int16_t> mutual_neighbor_friend_count(const NeighborContext&,std::int16_t neighbor) const;
    std::size_t source_count() const { return sources_.size(); }
    std::size_t pair_count() const { return pairs_; }
    std::size_t value_count() const { return values_; }
private:
    struct Source {
        RelationshipMatrix matrix;
        std::int16_t id;
        std::uint64_t generation;
        bool operator<(const Source& b) const {
            if (matrix != b.matrix) return matrix < b.matrix;
            if (id != b.id) return id < b.id;
            return generation < b.generation;
        }
    };
    using Pairs = std::map<std::int32_t,std::vector<std::int16_t>>;
    std::map<Source,Pairs> sources_;
    RelationshipLimits limits_;
    std::size_t pairs_ = 0, values_ = 0;
    bool fresh_ = true;
    std::optional<std::int16_t> lookup(Source, std::int32_t, std::size_t) const;
};
}
