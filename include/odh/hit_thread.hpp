#pragma once

#include "odh/hit_selection.hpp"
#include <memory>

namespace odh {
enum class HitNoteStatus { active, complete, failed };
struct HitNoteBackend {
    // nullopt explicitly reports a failed start: write -1, retain LastNote.
    // Successful handles must be unique and queryable for the thread lifetime.
    std::function<std::optional<std::uint64_t>(std::uint32_t patch)> start;
    std::function<HitNoteStatus(std::uint64_t handle)> status;
    // Required only if opcode69 occurs; applies the requested effect or throws.
    std::function<void(std::uint8_t group)> kill_group;
};
struct HitThreadLimits {
    HitSelectionLimits execution{}; // Cumulative, including repeated WaitSamp.
    std::uint32_t notes = 64; // Successful notes over the entire lifetime.
};
enum class HitThreadTick { yielded, complete };

// Bounded Simitone-submodule HIT lifecycle profile, not original equivalence.
// Implements the selection instruction subset plus WaitSamp and stackless End.
// Call/Return, loops and controllers/mixer unsupported; group effects require
// an explicit backend callback. No implicit success or fallback is provided.
// HIT/HOT must outlive the thread and remain immutable. Other input values and
// callbacks are copied. One owner; callbacks may not reenter tick or mutate
// resources. Backend ownership/cancellation remains with the caller.
class HitThread {
public:
    HitThread(const HitFile&, const Hot&, std::uint32_t start_pc,
              const std::array<std::int32_t, 29>& object_vars,
              HitChooser, HitNoteBackend, HitThreadLimits = {},
              const HotTrack* initial_track = nullptr,
              HitJumpOperand = HitJumpOperand::FreeSoHeuristic,
              HitSourceFieldReader = {}, std::optional<HitArguments> = {},
              std::optional<HitNativeControls> = {});
    ~HitThread();
    HitThread(const HitThread&) = delete;
    HitThread& operator=(const HitThread&) = delete;
    // Throws on unsupported/malformed/budget/backend failure; latches fault,
    // so a later call cannot retry effects or masquerade as completion.
    HitThreadTick tick();
    // Primitive for the already admitted, active, unpaused native ArgStyle 2
    // controller with interrupt ID 55 clear and its own provider (ID 53 zero).
    // The caller establishes those external controller/admission/group gates;
    // this method cannot infer them from HIT ticks or isplaying ID 46. Updates
    // raw R1..R3, clears fade ID 45, then sets spatial IDs 19/20 and flags.
    // No scheduler, Clock, backend, chooser, binding or provider effect runs.
    // Ordinary precondition rejection leaves state and fault latch unchanged;
    // callback reentry latches fault just like reentrant tick.
    void update_native_active_unpaused(const HitArguments& raw,
                                       std::int32_t spatial_external_volume,
                                       std::int32_t spatial_pan);
    std::uint32_t pc() const;
    bool bytecode_ended() const;
    bool faulted() const;
    std::size_t notes_started() const;
    const HitNotePlan& state() const; // Snapshot fields/trace; not offline proof.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
