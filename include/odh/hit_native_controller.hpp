#pragma once

#include "odh/hit_code_target.hpp"
#include "odh/hit_object_catalog.hpp"
#include "odh/hit_selection.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace odh {

struct NativeHitState;

// Ordered operations for global-Patch NoteOn2. The provider resolves the
// controller's public getter (including any fallback) and the current Patch's
// public getter independently; neither is inferred from a raw bank or HOT.
// wrapper_write receives the exact native arguments (38, 1, 0). Its effects
// belong to the wrapper provider, not to a generic PCM/streaming switch.
// wrapper_note_on includes the wrapper's own backend operations and returns
// its play result; NoteOn2 ignores that result and still reads Patch ID38.
// When that live post-play value is nonzero, controller_a then controller_b
// run with separate copied snapshots. These are the native A/B recalculations,
// not aliases for the bind phases or ID19/20 setters.
// All six callbacks must be present before a bound global NoteOn2 executes.
// A callback may change external Patch/wrapper/provider state, but must not
// reenter controller mutation or modify the immutable HIT/HOT/catalog data.
struct NativeHitGlobalNoteOn {
    std::function<std::int32_t(const NativeHitState&)> controller_id38;
    std::function<void(std::uint8_t id, std::int32_t value, std::int32_t argument)>
        wrapper_write;
    std::function<bool(std::uint8_t literal)> wrapper_note_on;
    std::function<std::int32_t()> patch_id38;
    std::function<void(const NativeHitState&)> controller_a;
    std::function<void(const NativeHitState&)> controller_b;
};

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
    std::function<void(std::uint8_t literal)> note_on; // Local-HOT profile only.
    std::function<void()> stop_current;
    std::function<void()> release_binding;
    std::function<void()> pause_current;
    std::function<void()> resume_current;
    std::function<void(std::uint8_t id, std::int32_t raw)> raw_control;
    // Global-Patch profile only. bind_global_patch acquires the winning Patch,
    // applies the first raw-control phase, then returns its effective ID26.
    // apply_global_controls performs the second phase after ID26 is stored.
    // Each state argument is a copied snapshot valid only for that call. The
    // same strong exception and reentry rules as bind apply to these callbacks.
    std::function<std::int32_t(const NativeHitPatchRef&, const NativeHitState&)>
        bind_global_patch = {};
    std::function<void(const NativeHitState&)> apply_global_controls = {};
    // Global profile: post-store snapshot for the native A-only recalculation
    // triggered by ID19/20. No Patch-ID26 copy or B/pitch update is implied.
    // Required in this profile; numeric raw_control belongs to local mode.
    std::function<void(std::uint8_t trigger_id, const NativeHitState&)>
        apply_global_control_write = {};
    // Required when NoteOn2 encounters an acquired global binding. Missing
    // callbacks fault before the first NoteOn getter/writer/play effect;
    // the local note_on callback is never a fallback for the global sequence.
    NativeHitGlobalNoteOn global_note_on = {};
};

struct NativeHitPatchSource {
    enum class Scope { local_hot, global_catalog };
    Scope scope = Scope::local_hot;
    // Borrowed; this and its underlying HOT/HIT resources must remain immutable
    // and outlive the controller and every callback using a Patch reference.
    const NativeHitObjectCatalog* catalog = nullptr;
};

enum class NativeHitBindingOutcome {
    not_attempted, zero, acquired, missing, wrong_kind
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

// Resource-aware alternative to NativeHitCallResolver. A resolved target owns
// neither its HitFile nor its bytes; every returned file must remain immutable
// and outlive this controller, including any saved return frames. Supply only
// one of the two resolver profiles. No HOT, ENTP or patch switching is implied.
using NativeHitResourceResolver =
    std::function<std::optional<HitCodeTarget>(std::uint32_t key)>;

struct NativeHitTrace {
    std::uint32_t pc = 0;
    std::uint32_t next_pc = 0;
    std::uint8_t opcode = 0;
    const HitFile* source = nullptr;
    const HitFile* next_source = nullptr;
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
    NativeHitBindingOutcome last_binding_outcome = NativeHitBindingOutcome::not_attempted;
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
// A resource resolver changes only the active bytecode file and PC. The entry
// file, HOT track, banks and audio binding retain their original identity.
// Each target is checked against that file's HIT 1.0/1.8 code region before a
// frame is pushed. Return cursors are checked only when fetched. Call stack
// and trace storage are preallocated to the configured bounded limits (default
// 64 frames and 4096 traces, maxima 256 and 65536). Each frame adds a borrowed
// file pointer beside its PC; each trace adds source and next-source pointers.
class NativeHitController {
public:
    NativeHitController(const HitFile&, const Hot&, const HotTrack&,
                        std::uint32_t entry_pc, const HitArguments& raw_args,
                        std::int32_t spatial_volume, std::int32_t spatial_pan,
                        NativeHitAudio, NativeHitLimits = {},
                        const HitNativeControllerValues& overrides = {},
                        NativeHitCallResolver call_resolver = {},
                        NativeHitResourceResolver resource_resolver = {},
                        NativeHitPatchSource patch_source = {});
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
    // Type1 caller boundary: the initializer's external group gate may return
    // early, but the caller still writes spatial controls and activates. This
    // also accepts a prepared controller that has never been activated. The
    // bool is an explicitly resolved gate, not an inferred HOT/group policy.
    void dispatch_type1(const HitArguments& raw_args, std::int32_t spatial_volume,
                        std::int32_t spatial_pan, std::uint32_t clock,
                        bool initializer_enabled);
    // Source-table effects. Neither call changes the contribution table;
    // request_fade_stop uses the same gradual-stop path as native +0x34.
    void set_spatial_controls(std::int32_t volume, std::int32_t pan);
    void request_fade_stop();
    void stop(); // Deactivates binding but retains controller and TrackID state.

    const NativeHitState& state() const noexcept { return state_; }
    const std::vector<NativeHitTrace>& trace() const noexcept { return trace_; }
    const HitFile& current_hit() const noexcept { return *active_hit_; }

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
    NativeHitResourceResolver resource_resolver_;
    NativeHitPatchSource patch_source_;
    NativeHitState state_;
    std::vector<NativeHitTrace> trace_;
    std::vector<HitCodeTarget> call_stack_;
    std::uint32_t entry_ = 0;
    std::uint32_t code_end_ = 0;
    const HitFile* active_hit_ = nullptr;
    bool busy_ = false;
    bool ever_activated_ = false;

    void enter();
    void leave() noexcept;
    void check_live() const;
    static std::uint32_t validate_target(HitCodeTarget target);
    void validate_pc(std::uint32_t pc) const;
    std::uint8_t byte();
    std::uint32_t word();
    std::int32_t read(std::uint8_t id) const;
    void write(std::uint8_t id, std::int32_t value);
    void set_flags(std::int32_t value) noexcept;
    void callback(const std::function<void()>& fn);
    bool audio_active();
    void note_on_current(std::uint8_t literal);
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
    void dispatch_type1_impl(const HitArguments&, std::int32_t, std::int32_t,
                             std::uint32_t, bool);
};

} // namespace odh
