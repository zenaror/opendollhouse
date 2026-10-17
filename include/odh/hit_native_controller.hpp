#pragma once

#include "odh/hit_selection.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace odh {

// One externally owned current audio binding. These callbacks describe requests
// at the HIT/controller boundary; they do not promise a voice count or DSP scale.
// bind must provide the strong exception guarantee: if it throws, it has left
// no acquired binding; on successful return ownership passes to the controller.
// stop_current and release_binding tear down that successfully acquired binding.
// release_binding must leave no acquired binding when it returns; if it throws,
// the external owner must resolve its possibly partial teardown.
// A swallowed reentrant bind is detected after return and is torn down before
// the controller rejects the callback. Callbacks must not mutate HIT/HOT.
// The external owner must keep backend objects and callback captures alive for
// the controller lifetime and arrange final backend cleanup; destruction of
// this portable state object does not itself own a physical audio resource.
struct NativeHitAudio {
    std::function<void(std::uint32_t patch_id)> bind;
    std::function<bool()> active;
    std::function<void(std::uint8_t literal)> note_on;
    std::function<void()> stop_current;
    std::function<void()> release_binding;
    std::function<void()> pause_current;
    std::function<void()> resume_current;
    std::function<void(std::uint8_t id, std::int32_t raw)> raw_control;
};

struct NativeHitLimits {
    std::uint32_t instructions_total = 4096;
    std::uint32_t instructions_per_poll = 256;
    std::uint32_t trace_entries = 4096;
    std::uint32_t call_depth = 64;
};

// Resolves the DWORD operand of Call7 to a bytecode offset. An absent key
// returns nullopt. Captured references and their backing map must outlive the
// controller. The caller supplies this map explicitly; HOT/ENTP precedence is
// not inferred here. The callback must not mutate the controller or HIT/HOT.
using NativeHitCallResolver = std::function<std::optional<std::uint32_t>(std::uint32_t key)>;

struct NativeHitTrace {
    std::uint32_t pc = 0;
    std::uint32_t next_pc = 0;
    std::uint8_t opcode = 0;
};

struct NativeHitState {
    // Bank IDs 1..16 is persistent across restart and stop; index 0 is ID 1.
    std::array<std::int32_t, 16> persistent{};
    std::array<std::int32_t, 31> controls_17_47{};
    std::array<std::int32_t, 13> controls_50_62{};
    std::uint32_t pc = 0;
    std::uint32_t deadline = 0;
    std::uint32_t instructions = 0;
    std::uint32_t call_depth = 0;
    bool active = false;
    bool paused = true;
    bool binding = false;
    bool fade_stop_pending = false;
    bool zero = false;
    bool positive = false;
    bool sign = false;
    bool faulted = false;
    bool native_diagnostic_stop = false; // Last stop was native diagnostic; cleared on restart.
};

enum class NativeHitPoll { inactive, paused, waiting, progressed, stopped, diagnostic_stopped };

// A single already admitted, own-provider ArgStyle 2 HOT controller. The caller
// resolves TrackID/entry PC from HOT entry metadata and admission/group gates,
// owns HIT/HOT/callback lifetime, and supplies each native Clock value
// explicitly. HOT hitlist_id must be resolved but is an entry-map key here;
// it is not a chooser request and never substitutes for current patch ID50.
// No World-clock
// cadence, manager, source provider or shared ID 53 provider is inferred.
// Constructor prepares fresh banks without audio effects, with ID46=0 and
// IDs19/20 holding raw R2/R3. This is a staged ODH snapshot, not an executed
// HOT parser/initializer. activate(clock) rebuilds fresh control banks from
// retained Track metadata even after stop() on the prepared state, then binds,
// preserves the persistent R4..R16 bank, writes ID46/active, pauses,
// applies the caller's spatial 19/20 setters and resumes in that order.
// Clock is uint32; a poll
// with its high bit set takes the native default diagnostic/stop path after
// fade handling. Deadline addition retains uint32 wrap and zero.
// Supported bytecode: NoteOn2, LoadL5, Call7 with an explicit resolver,
// Return8, Wait9, WaitSamp11, End12, StopRequest27. Other opcodes and
// unmodelled variable side effects fault.
// Timed-NoteOn1 is excluded, so the native +194 deadline remains zero here.
// The default native diagnostic after instruction 102 stops the controller;
// lower configured ODH safety budgets can instead fault before that point.
// All execution/callback faults latch; no later mutator produces effects.
// Exceptional teardown of a binding acquired during a failing operation is
// still attempted to avoid leaking that external resource.
class NativeHitController {
public:
    NativeHitController(const HitFile&, const Hot&, const HotTrack&,
                        std::uint32_t entry_pc, const HitArguments& raw_args,
                        std::int32_t spatial_volume, std::int32_t spatial_pan,
                        NativeHitAudio, NativeHitLimits = {},
                        const HitNativeControllerValues& overrides = {},
                        NativeHitCallResolver call_resolver = {});
    NativeHitController(const NativeHitController&) = delete;
    NativeHitController& operator=(const NativeHitController&) = delete;

    void activate(std::uint32_t clock);
    NativeHitPoll poll(std::uint32_t clock);
    void pause();
    void resume(std::uint32_t clock);
    // Caller has already admitted this event and passed external group gates.
    // Active/unpaused ID55=0 updates arguments without reset. Active/paused
    // resumes before consulting ID55. Inactive or active ID55!=0 restarts.
    void retrigger(const HitArguments& raw_args, std::int32_t spatial_volume,
                   std::int32_t spatial_pan, std::uint32_t clock);
    void stop(); // Deactivates binding but retains controller and TrackID state.

    const NativeHitState& state() const noexcept { return state_; }
    const std::vector<NativeHitTrace>& trace() const noexcept { return trace_; }

private:
    const HitFile& hit_;
    const Hot& hot_;
    HotTrack track_;
    HitNativeControllerValues overrides_;
    HitArguments initial_args_{};
    std::int32_t initial_volume_ = 0;
    std::int32_t initial_pan_ = 0;
    NativeHitAudio audio_;
    NativeHitLimits limits_;
    NativeHitCallResolver call_resolver_;
    NativeHitState state_;
    std::vector<NativeHitTrace> trace_;
    std::vector<std::uint32_t> call_stack_;
    std::uint32_t entry_ = 0;
    std::uint32_t code_end_ = 0;
    bool busy_ = false;
    bool ever_activated_ = false;

    void enter();
    void leave() noexcept;
    void check_live() const;
    void validate_pc(std::uint32_t pc) const;
    std::uint8_t byte();
    std::uint32_t word();
    std::int32_t read(std::uint8_t id) const;
    void write(std::uint8_t id, std::int32_t value);
    void set_flags(std::int32_t value) noexcept;
    void callback(const std::function<void()>& fn);
    bool audio_active();
    void bind_current();
    void release_current();
    void stop_and_release();
    void stop_impl();
    void request_stop();
    void advance_fade();
    void pause_impl();
    void resume_impl(std::uint32_t);
    void update_args_clear(const HitArguments&);
    void write_spatial(std::int32_t, std::int32_t);
    void restart(const HitArguments&, std::int32_t, std::int32_t, std::uint32_t);
};

} // namespace odh
