#pragma once
#include "odh/neighbor_context.hpp"
#include "odh/world.hpp"

namespace odh {
enum class RelationshipMatrix { neighbor, object };
struct RelationshipLimits {
    std::size_t sources = 4096, pairs = 16384, values = 65536;
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
// are runtime-only, keyed by source identity/generation and target ushort ID;
// no TSO persistence, TS1 save writer or inbound relationship index is implied.
class RelationshipState {
public:
    explicit RelationshipState(const NeighborContext&, RelationshipLimits = {});
    RelationshipState(const RelationshipState&) = delete;
    RelationshipState& operator=(const RelationshipState&) = delete;
    RelationshipState(RelationshipState&&) = delete;
    RelationshipState& operator=(RelationshipState&&) = delete;
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
    std::optional<std::int16_t> lookup(Source, std::int32_t, std::size_t) const;
};
}
