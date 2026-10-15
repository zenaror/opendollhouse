#include "odh/hit_source_contributions.hpp"
#include "odh/formats.hpp"
#include <algorithm>
#include <limits>

namespace odh {
namespace {
std::int32_t signed_bits(std::uint32_t bits) {
    if (bits <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return static_cast<std::int32_t>(bits);
    return -1 - static_cast<std::int32_t>(~bits);
}
}
HitSourceContributions::HitSourceContributions(std::size_t max_pairs):max_pairs_(max_pairs) {
    if (!max_pairs || max_pairs > hard_limit)
        throw FormatError("HIT source contributions: invalid pair budget");
    pairs_.reserve(max_pairs_);
}
bool HitSourceContributions::contains(std::uint32_t track_id, std::int32_t source_r1) const noexcept {
    return std::any_of(pairs_.begin(), pairs_.end(), [=](const auto& pair) {
        return pair.track_id == track_id && pair.source_r1 == source_r1;
    });
}
HitSourceAdd HitSourceContributions::add(std::uint32_t track_id, std::int32_t source_r1) {
    if (contains(track_id, source_r1)) return HitSourceAdd::already_present;
    if (pairs_.size() == max_pairs_) return HitSourceAdd::capacity_refused;
    pairs_.push_back({track_id, source_r1}); // Constructor reserved the full bound.
    return HitSourceAdd::inserted;
}
std::size_t HitSourceContributions::count_track(std::uint32_t track_id) const noexcept {
    return static_cast<std::size_t>(std::count_if(pairs_.begin(), pairs_.end(), [=](const auto& pair) {
        return pair.track_id == track_id;
    }));
}
std::size_t HitSourceContributions::copy_track(std::uint32_t track_id, HitSourcePair* output,
                                                std::size_t output_capacity) const {
    const auto count = count_track(track_id);
    if (count > output_capacity || (count && !output))
        throw FormatError("HIT source contributions: track output buffer too small");
    std::size_t written = 0;
    for (const auto& pair : pairs_) if (pair.track_id == track_id) output[written++] = pair;
    return written;
}
bool HitSourceContributions::erase(std::uint32_t track_id, std::int32_t source_r1) noexcept {
    const auto found = std::find_if(pairs_.begin(), pairs_.end(), [=](const auto& pair) {
        return pair.track_id == track_id && pair.source_r1 == source_r1;
    });
    if (found == pairs_.end()) return false;
    pairs_.erase(found);
    return true;
}
std::size_t HitSourceContributions::clear_track(std::uint32_t track_id) noexcept {
    const auto before = pairs_.size();
    pairs_.erase(std::remove_if(pairs_.begin(), pairs_.end(), [=](const auto& pair) {
        return pair.track_id == track_id;
    }), pairs_.end());
    return before - pairs_.size();
}
std::optional<std::uint32_t> HitSourceContributions::remove_one_source(std::int32_t source_r1) noexcept {
    const auto found = std::find_if(pairs_.begin(), pairs_.end(), [=](const auto& pair) {
        return pair.source_r1 == source_r1;
    });
    if (found == pairs_.end()) return std::nullopt;
    const auto track_id = found->track_id;
    pairs_.erase(found);
    return track_id;
}
HitSourceAggregate aggregate_hit_source_controls(const HitSourceControls* samples,
                                                 std::size_t count) {
    if (count > HitSourceContributions::hard_limit || (count && !samples))
        throw FormatError("HIT source aggregate: invalid input buffer or count");
    std::int32_t maximum = 0;
    std::uint32_t denominator = 0, numerator = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto volume = samples[index].external_volume;
        if (volume > maximum) maximum = volume;
        denominator += static_cast<std::uint32_t>(volume);
        const auto offset = static_cast<std::uint32_t>(samples[index].pan) - std::uint32_t{512};
        numerator += offset * static_cast<std::uint32_t>(volume);
    }
    const auto divisor = signed_bits(denominator);
    if (!divisor) return {maximum, 512};
    const auto dividend = signed_bits(numerator);
    if (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1)
        throw FormatError("HIT source aggregate: signed division overflow");
    const auto quotient = dividend / divisor; // C++17 truncates toward zero, as IDIV does.
    return {maximum, signed_bits(static_cast<std::uint32_t>(quotient) + std::uint32_t{512})};
}
}
