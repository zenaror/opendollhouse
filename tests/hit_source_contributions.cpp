#include "odh/hit_source_contributions.hpp"
#include "odh/formats.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace {
bool watch_allocations = false;
std::size_t watched_allocations = 0;
}
void* operator new(std::size_t size) {
    if (watch_allocations) ++watched_allocations;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    if (watch_allocations) ++watched_allocations;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
int checks = 0;
void require(bool condition, const char* why) {
    ++checks;
    if (!condition) throw std::runtime_error(why);
}
template<class F> void rejects(F&& operation, const char* why) {
    bool failed = false;
    try { operation(); } catch (const odh::FormatError&) { failed = true; }
    require(failed, why);
}
std::int32_t wrap_reference(std::int64_t value) {
    constexpr std::int64_t modulus = 4294967296LL;
    value %= modulus;
    if (value < 0) value += modulus;
    if (value >= 2147483648LL) value -= modulus;
    return static_cast<std::int32_t>(value);
}
odh::HitSourceAggregate reference_aggregate(const std::vector<odh::HitSourceControls>& inputs) {
    std::int64_t maximum = 0, weight_sum = 0, weighted_pan = 0;
    for (const auto& input : inputs) {
        if (input.external_volume > maximum) maximum = input.external_volume;
        weight_sum += input.external_volume;
        weighted_pan += (static_cast<std::int64_t>(input.pan) - 512) * input.external_volume;
    }
    const auto divisor = wrap_reference(weight_sum);
    if (!divisor) return {static_cast<std::int32_t>(maximum), 512};
    const auto numerator = wrap_reference(weighted_pan);
    if (numerator == std::numeric_limits<std::int32_t>::min() && divisor == -1)
        throw odh::FormatError("reference IDIV overflow");
    return {static_cast<std::int32_t>(maximum),
            wrap_reference(static_cast<std::int64_t>(numerator) / divisor + 512)};
}
}
int main() { try {
    static_assert(!std::is_copy_constructible<odh::HitSourceContributions>::value &&
                  !std::is_copy_assignable<odh::HitSourceContributions>::value &&
                  !std::is_move_constructible<odh::HitSourceContributions>::value &&
                  !std::is_move_assignable<odh::HitSourceContributions>::value,
                  "fixed pair capacity cannot be lost through copying or moving");
    rejects([] { odh::HitSourceContributions bad(0); }, "zero pair budget rejected");
    rejects([] { odh::HitSourceContributions bad(odh::HitSourceContributions::hard_limit + 1); },
            "over-hard-limit pair budget rejected");
    {
        odh::HitSourceContributions pairs(3);
        require(pairs.capacity() == 3 && pairs.size() == 0, "explicit capacity preallocated");
        require(pairs.add(7, 10) == odh::HitSourceAdd::inserted &&
                pairs.add(8, 10) == odh::HitSourceAdd::inserted &&
                pairs.add(7, 20) == odh::HitSourceAdd::inserted,
                "different tracks and sources produce distinct pairs");
        require(pairs.add(7, 10) == odh::HitSourceAdd::already_present &&
                pairs.add(9, 10) == odh::HitSourceAdd::capacity_refused && pairs.size() == 3,
                "duplicate remains idempotent at full capacity and new pair is refused");
        require(pairs.contains(7, 10) && !pairs.contains(8, 20) && pairs.count_track(7) == 2,
                "lookup compares both raw keys");
        odh::HitSourcePair output[3]{{99, 99}, {99, 99}, {99, 99}};
        rejects([&] { pairs.copy_track(7, output, 1); }, "short lookup buffer rejected");
        require(output[0].track_id == 99 && output[0].source_r1 == 99,
                "short lookup buffer receives no partial writes");
        rejects([&] { pairs.copy_track(7, nullptr, 2); }, "null nonempty lookup buffer rejected");
        require(pairs.copy_track(7, output, 3) == 2 &&
                output[0].source_r1 == 10 && output[1].source_r1 == 20,
                "bounded copy preserves deterministic ODH insertion order");
        require(pairs.erase(7, 10) && !pairs.erase(7, 10) && pairs.count_track(7) == 1,
                "explicit pair removal is exact and idempotent");
        require(pairs.add(9, 10) == odh::HitSourceAdd::inserted,
                "freed capacity admits a new pair");
        require(pairs.remove_one_source(10) == std::optional<std::uint32_t>{8} &&
                pairs.count_track(9) == 1 && pairs.count_track(7) == 1,
                "one-source removal leaves other tracks for a later effect step");
        require(pairs.remove_one_source(10) == std::optional<std::uint32_t>{9} &&
                !pairs.remove_one_source(10), "repeated one-source removal covers all tracks without effect callback");
        require(pairs.clear_track(7) == 1 && pairs.clear_track(7) == 0 && pairs.size() == 0,
                "track clear removes only its contributions");
        require(pairs.copy_track(7, nullptr, 0) == 0, "empty lookup accepts null zero-capacity output");
    }
    {
        odh::HitSourceContributions pairs(4);
        const auto lowest = std::numeric_limits<std::int32_t>::min();
        const auto highest = std::numeric_limits<std::int32_t>::max();
        require(pairs.add(0, 0) == odh::HitSourceAdd::inserted &&
                pairs.add(std::numeric_limits<std::uint32_t>::max(), lowest) == odh::HitSourceAdd::inserted &&
                pairs.add(0, highest) == odh::HitSourceAdd::inserted,
                "storage retains full raw TrackID/R1 bit ranges without World validation");
        require(pairs.contains(0, 0) && pairs.contains(0, highest) &&
                pairs.remove_one_source(lowest) == std::optional<std::uint32_t>{
                    std::numeric_limits<std::uint32_t>::max()},
                "zero, negative and extreme raw values stay distinct");
    }
    {
        odh::HitSourceContributions pairs(3);
        odh::HitSourcePair output[3]{};
        watched_allocations = 0;
        watch_allocations = true;
        const auto a = pairs.add(1, 10), b = pairs.add(1, 20), c = pairs.add(2, 10);
        const auto duplicate = pairs.add(1, 10), full = pairs.add(3, 10);
        const auto copied = pairs.copy_track(1, output, 3);
        const auto removed = pairs.remove_one_source(10);
        const auto erased = pairs.erase(1, 20);
        const auto cleared = pairs.clear_track(2);
        watch_allocations = false;
        require(!watched_allocations && a == odh::HitSourceAdd::inserted &&
                b == odh::HitSourceAdd::inserted && c == odh::HitSourceAdd::inserted &&
                duplicate == odh::HitSourceAdd::already_present &&
                full == odh::HitSourceAdd::capacity_refused && copied == 2 &&
                removed == std::optional<std::uint32_t>{1} && erased && cleared == 1,
                "successful, duplicate, full and removal mutations allocate nothing after construction");
    }
    {
        const auto zero = odh::aggregate_hit_source_controls(nullptr, 0);
        require(zero.external_volume == 0 && zero.pan == 512, "empty raw aggregate is 0/512");
        rejects([] { odh::aggregate_hit_source_controls(nullptr, 1); },
                "nonempty raw aggregate rejects null input");
        const odh::HitSourceControls one{1, 512};
        rejects([&] { odh::aggregate_hit_source_controls(&one, odh::HitSourceContributions::hard_limit + 1); },
                "raw aggregate rejects over-hard-limit count before dereference");
        const odh::HitSourceControls negative[]{{-100, 1000}, {-200, 0}};
        const auto result = odh::aggregate_hit_source_controls(negative, 2);
        require(result.external_volume == 0 && result.pan == 334,
                "signed negative weights affect pan but cannot lower volume maximum below zero");
        const odh::HitSourceControls cancelled[]{{100, 0}, {-100, 1024}};
        const auto neutral = odh::aggregate_hit_source_controls(cancelled, 2);
        require(neutral.external_volume == 100 && neutral.pan == 512,
                "zero denominator retains positive volume maximum and neutral pan");
        const auto highest = std::numeric_limits<std::int32_t>::max();
        const auto lowest = std::numeric_limits<std::int32_t>::min();
        const odh::HitSourceControls wrapped[]{{highest, highest}, {highest, lowest}};
        const auto overflow = odh::aggregate_hit_source_controls(wrapped, 2);
        require(overflow.external_volume == highest && overflow.pan == 1073741823,
                "full-bit products and sums wrap before signed IDIV");
        const odh::HitSourceControls wrap_zero[]{{highest, 512}, {highest, 512}, {2, 512}};
        const auto zero_denominator = odh::aggregate_hit_source_controls(wrap_zero, 3);
        require(zero_denominator.external_volume == highest && zero_denominator.pan == 512,
                "wrapped denominator zero does not zero signed volume maximum");
        const odh::HitSourceControls pan_wrap{1, lowest + 511};
        const auto no_clamp = odh::aggregate_hit_source_controls(&pan_wrap, 1);
        require(no_clamp.external_volume == 1 && no_clamp.pan == lowest + 511,
                "final pan addition wraps without clamp");
        const odh::HitSourceControls mixed_wrap[]{{2, 1073742336}, {-1, 513}};
        const auto mixed = odh::aggregate_hit_source_controls(mixed_wrap, 2);
        require(mixed.external_volume == 2 && mixed.pan == lowest + 511,
                "mixed-sign products wrap numerator to INT32_MAX before pan addition");
        const odh::HitSourceControls division_trap{-1, lowest + 512};
        rejects([&] { odh::aggregate_hit_source_controls(&division_trap, 1); },
                "INT32_MIN divided by -1 fails explicitly instead of undefined behavior");
    }
    {
        std::mt19937 generator(0x57488fU);
        std::uniform_int_distribution<std::int32_t> controls(-1000000, 1000000);
        for (std::size_t trial = 0; trial < 500; ++trial) {
            std::vector<odh::HitSourceControls> inputs;
            const auto count = static_cast<std::size_t>(generator() % 32U);
            inputs.reserve(count);
            for (std::size_t index = 0; index < count; ++index)
                inputs.push_back({controls(generator), controls(generator)});
            const auto expected = reference_aggregate(inputs);
            const auto actual = odh::aggregate_hit_source_controls(inputs.data(), inputs.size());
            require(actual.external_volume == expected.external_volume && actual.pan == expected.pan,
                    "bounded randomized aggregate matches independent int64 modulo oracle");
        }
    }
    std::cout << checks << " HIT source contribution and raw aggregate checks passed\n";
    return 0;
} catch (const std::exception& error) {
    watch_allocations = false;
    std::cerr << error.what() << '\n';
    return 1;
} }
