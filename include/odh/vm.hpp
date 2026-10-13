#pragma once

#include "odh/formats.hpp"
#include <map>

namespace odh {

enum class RoutineScope { global, private_object, semiglobal };
struct RoutineKey {
    RoutineScope scope;
    std::uint16_t id;
    bool operator<(const RoutineKey& other) const {
        return scope != other.scope ? scope < other.scope : id < other.id;
    }
};
// Explicit resource set for one code owner. No implicit file precedence.
using RoutineSet = std::map<RoutineKey, Bhav>;
RoutineKey routine_key(std::uint16_t id);

enum class VmStatus { ready, running, budget_exhausted, returned_true, returned_false, fault };
const char* vm_status_name(VmStatus);
struct VmContext {
    std::int16_t caller = 0;
    std::int16_t callee = 0;
    std::int16_t stack_object = 0;
};
struct VmFrame {
    RoutineKey key;
    std::uint16_t pc = 0;
    VmContext context;
    std::vector<std::int16_t> arguments;
    std::vector<std::int16_t> locals;
};
struct VmTrace {
    std::uint64_t step;
    std::size_t depth;
    RoutineKey key;
    std::uint16_t pc;
    Instruction instruction;
};
// World scopes are supplied explicitly by the eventual entity/world layer.
// Empty callbacks or nullopt mean unsupported, never zero or success.
struct VmMemory {
    std::function<std::optional<std::int16_t>(const VmFrame&, std::uint8_t, std::int16_t)> read;
    std::function<bool(const VmFrame&, std::uint8_t, std::int16_t, std::int16_t)> write;
};
struct VmLimits {
    std::size_t stack_depth = 64;
    std::size_t frame_variables = 4096;
};

// Host-independent, resumable instruction kernel. Owns an immutable routine set.
// No scheduler, entities, queue, routing, animation, or clock is implied.
class Vm {
public:
    explicit Vm(RoutineSet routines, VmMemory memory = {}, VmLimits limits = {});
    VmStatus start(RoutineKey, VmContext = {}, const std::vector<std::int16_t>& args = {});
    VmStatus run(std::uint64_t instruction_budget,
                 const std::function<void(const VmTrace&)>& trace = {});
    VmStatus status() const { return status_; }
    const std::string& diagnostic() const { return diagnostic_; }
    const std::vector<VmFrame>& stack() const { return stack_; }
    std::array<std::int16_t, 20>& temps() { return temps_; }
    const std::array<std::int16_t, 20>& temps() const { return temps_; }
    std::uint64_t steps() const { return steps_; }
private:
    void push(RoutineKey, VmContext, const std::vector<std::int16_t>&);
    void branch(bool);
    bool expression(const Instruction&);
    std::int16_t read(std::uint8_t, std::int16_t);
    void write(std::uint8_t, std::int16_t, std::int32_t);
    RoutineSet routines_;
    VmMemory memory_;
    VmLimits limits_;
    std::vector<VmFrame> stack_;
    std::array<std::int16_t, 20> temps_{};
    VmStatus status_ = VmStatus::ready;
    std::string diagnostic_;
    std::uint64_t steps_ = 0;
    bool executing_ = false;
};
} // namespace odh
