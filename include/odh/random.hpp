#pragma once
#include "odh/vm.hpp"

namespace odh {
// Explicit, copyable snapshot of FreeSO 4c6b3e8 VMContext.NextRandom state.
// No clock/entropy/default seed is inferred. Zero is a valid absorbing seed.
class RandomState {
public:
    explicit RandomState(std::uint64_t seed) : seed_(seed) {}
    std::uint64_t seed() const { return seed_; }
    void reset(std::uint64_t seed) { seed_ = seed; }
    std::uint64_t next(std::uint64_t range);
private:
    std::uint64_t seed_;
};
struct RandomRecord {
    VmContext context;
    std::uint16_t range;
    std::int16_t result;
    std::uint8_t destination_scope;
    std::int16_t destination_data;
    std::uint64_t seed_before, seed_after;
};
using RandomObserver = std::function<void(const RandomRecord&)>;
// Opcode 8; shared state must outlive every handler. Serial dispatch only.
// Callback memory is explicit; false writes must leave memory unchanged.
// Callbacks must not reenter/mutate this RNG, and observers must not throw.
// Context destination scope 10 is unsupported: Vm has no context output.
VmPrimitive random_primitive(RandomState&, VmMemory memory = {}, RandomObserver observer = {});
} // namespace odh
