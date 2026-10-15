#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace odh {
// Raw TrackID/R1 pairs. No World identity, controller ownership, spatial
// provider, audio effect or native hash traversal order is implied.
struct HitSourcePair {
    std::uint32_t track_id;
    std::int32_t source_r1;
};
enum class HitSourceAdd { inserted, already_present, capacity_refused };

class HitSourceContributions {
public:
    static constexpr std::size_t hard_limit = 65536; // ODH allocation bound, not a native limit.
    explicit HitSourceContributions(std::size_t max_pairs);
    HitSourceContributions(const HitSourceContributions&) = delete;
    HitSourceContributions& operator=(const HitSourceContributions&) = delete;
    HitSourceContributions(HitSourceContributions&&) = delete;
    HitSourceContributions& operator=(HitSourceContributions&&) = delete;
    HitSourceAdd add(std::uint32_t track_id, std::int32_t source_r1);
    bool contains(std::uint32_t track_id, std::int32_t source_r1) const noexcept;
    std::size_t count_track(std::uint32_t track_id) const noexcept;
    // Copies the whole track in deterministic ODH insertion order. An output
    // buffer that is too small (or null for nonempty data) fails without writes.
    std::size_t copy_track(std::uint32_t track_id, HitSourcePair* output,
                           std::size_t output_capacity) const;
    bool erase(std::uint32_t track_id, std::int32_t source_r1) noexcept;
    std::size_t clear_track(std::uint32_t track_id) noexcept;
    // Removes one matching pair. Repeat explicitly if every track needs source
    // removal, applying any orchestrator effect between calls. Selection order
    // is ODH insertion order, not the original game's hash traversal order.
    std::optional<std::uint32_t> remove_one_source(std::int32_t source_r1) noexcept;
    std::size_t size() const noexcept { return pairs_.size(); }
    std::size_t capacity() const noexcept { return max_pairs_; }
private:
    std::size_t max_pairs_;
    std::vector<HitSourcePair> pairs_;
};

// Inputs are already resolved raw signed controls. No source lookup or
// backend conversion occurs here. The arithmetic reproduces the proven
// 32-bit aggregate only; application-time adjustment is separate.
struct HitSourceControls {
    std::int32_t external_volume;
    std::int32_t pan;
};
struct HitSourceAggregate {
    std::int32_t external_volume;
    std::int32_t pan;
};
HitSourceAggregate aggregate_hit_source_controls(const HitSourceControls* samples,
                                                 std::size_t count);
}
