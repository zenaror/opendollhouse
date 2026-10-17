#include "odh/hit_native_controller.hpp"
#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace odh {
namespace {
std::int32_t signed_bits(std::uint32_t bits) {
    if (bits <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        return static_cast<std::int32_t>(bits);
    return -1 - static_cast<std::int32_t>(~bits);
}
std::int32_t add32(std::int32_t a, std::int32_t b) {
    return signed_bits(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::int32_t neg32(std::int32_t a) {
    return signed_bits(0U - static_cast<std::uint32_t>(a));
}
void require(bool condition, const char* why) {
    if (!condition) throw FormatError(why);
}
}

NativeHitController::NativeHitController(const HitFile& hit, const Hot& hot,
        const HotTrack& track, std::uint32_t entry_pc, const HitArguments& args,
        std::int32_t volume, std::int32_t pan, NativeHitAudio audio,
        NativeHitLimits limits, const HitNativeControllerValues& overrides,
        NativeHitCallResolver call_resolver,
        NativeHitResourceResolver resource_resolver,
        NativeHitPatchSource patch_source)
    : hit_(hit), hot_(hot), track_(track), overrides_(overrides),
      initial_args_(args), initial_volume_(volume), initial_pan_(pan),
      audio_(std::move(audio)), limits_(limits),
      call_resolver_(std::move(call_resolver)),
      resource_resolver_(std::move(resource_resolver)),
      patch_source_(patch_source), entry_(entry_pc),
      active_hit_(&hit) {
    require(!call_resolver_ || !resource_resolver_,
            "native HIT accepts only one Call7 resolver profile");
    code_end_ = validate_target({&hit, entry_});
    require(limits_.instructions_total && limits_.instructions_total <= 1000000 &&
            limits_.instructions_per_poll && limits_.instructions_per_poll <= 10000 &&
            limits_.trace_entries && limits_.trace_entries <= 65536 &&
            limits_.call_depth && limits_.call_depth <= 256,
            "native HIT limits outside supported bounds");
    require(audio_.active && audio_.note_on && audio_.stop_current &&
            audio_.release_binding && audio_.pause_current && audio_.resume_current &&
            audio_.raw_control, "native HIT requires complete current-binding callbacks");
    require((patch_source_.scope == NativeHitPatchSource::Scope::local_hot &&
             patch_source_.catalog == nullptr) ||
            (patch_source_.scope == NativeHitPatchSource::Scope::global_catalog &&
             patch_source_.catalog != nullptr),
            "native HIT patch source profile is invalid");
    if (patch_source_.scope == NativeHitPatchSource::Scope::local_hot)
        require(static_cast<bool>(audio_.bind), "native HIT local patch bind callback is required");
    else
        require(audio_.bind_global_patch && audio_.apply_global_controls,
                "native HIT global patch callbacks are required");
    require(track.hitlist_id.has_value(),
            "native HIT requires resolved HOT entry metadata");
    const auto initial = build_native_hit_controls(track_, args, volume, pan, overrides_);
    require(*initial.locals[53 - 16] == 0, "native HIT requires own provider ID 53 zero");
    for (std::uint32_t id = 17; id <= 47; ++id)
        state_.controls_17_47[id - 17] = *initial.locals[id - 16];
    for (std::uint32_t id = 50; id <= 62; ++id)
        state_.controls_50_62[id - 50] = *initial.locals[id - 16];
    // The constructor is a pure, staged ODH snapshot. Activation performs the
    // initializer's isplaying/pause and the caller's spatial setters in order.
    state_.controls_17_47[19 - 17] = args[1];
    state_.controls_17_47[20 - 17] = args[2];
    state_.controls_17_47[46 - 17] = 0;
    state_.persistent[0] = args[0];
    state_.persistent[1] = args[1];
    state_.persistent[2] = args[2];
    state_.persistent[15] = *initial.locals[16 - 16];
    state_.pc = entry_;
    set_flags(args[2]);
    const auto patch = static_cast<std::uint32_t>(state_.controls_50_62[0]);
    if (patch_source_.scope == NativeHitPatchSource::Scope::local_hot)
        require(!patch || hot_.patches.find(patch) != hot_.patches.end(),
                "native HIT initial patch ID is unresolved");
    trace_.reserve(limits_.trace_entries);
    call_stack_.reserve(limits_.call_depth);
}

void NativeHitController::enter() {
    if (busy_) {
        state_.faulted = true;
        throw FormatError("native HIT callback reentered controller mutation");
    }
    if (state_.faulted) throw FormatError("native HIT controller is faulted");
    busy_ = true;
}
void NativeHitController::leave() noexcept { busy_ = false; }
void NativeHitController::check_live() const {
    if (state_.faulted) throw FormatError("native HIT controller is faulted");
}
std::uint32_t NativeHitController::validate_target(HitCodeTarget target) {
    require(target.file != nullptr, "native HIT Call7 target has no resource");
    const auto& hit = *target.file;
    require(hit.major_version == 1 && (hit.minor_version == 0 || hit.minor_version == 8),
            "native HIT requires supported HIT 1.0/1.8");
    require(hit.bytes.size() <= std::numeric_limits<std::uint32_t>::max(),
            "native HIT too large");
    const auto end = hit.entry_table_offset.value_or(hit.bytes.size());
    require(end <= hit.bytes.size() && end > 16, "native HIT has invalid code region");
    require(target.pc >= 16 && target.pc < end,
            "native HIT PC outside bytecode region");
    return static_cast<std::uint32_t>(end);
}
void NativeHitController::validate_pc(std::uint32_t pc) const {
    require(pc >= 16 && pc < code_end_, "native HIT PC outside bytecode region");
}
std::uint8_t NativeHitController::byte() {
    validate_pc(state_.pc);
    return active_hit_->bytes[state_.pc++];
}
std::uint32_t NativeHitController::word() {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<std::uint32_t>(byte()) << shift;
    return value;
}
std::int32_t NativeHitController::read(std::uint8_t id) const {
    if (id >= 1 && id <= 16) return state_.persistent[id - 1];
    if (id >= 17 && id <= 47) return state_.controls_17_47[id - 17];
    if (id >= 50 && id <= 62) return state_.controls_50_62[id - 50];
    throw FormatError("native HIT variable ID is outside supported banks");
}
void NativeHitController::set_flags(std::int32_t value) noexcept {
    state_.zero = value == 0;
    state_.positive = value > 0;
    state_.sign = value < 0;
}
void NativeHitController::callback(const std::function<void()>& fn) {
    check_live();
    fn();
    check_live(); // A callback may swallow a reentrant mutation exception.
}
bool NativeHitController::audio_active() {
    if (!state_.binding) return false;
    check_live();
    const bool result = audio_.active();
    check_live();
    return result;
}
void NativeHitController::write(std::uint8_t id, std::int32_t value) {
    // This whitelist avoids pretending that arbitrary public HIT writers have
    // no provider, patch, group, or audio side effects.
    const bool register_id = id >= 1 && id <= 16;
    const bool supported_control = id == 19 || id == 20 || id == 40 ||
        id == 41 || id == 42 || id == 45 || id == 55;
    require(register_id || supported_control, "native HIT write has unsupported side effects");
    if (register_id) state_.persistent[id - 1] = value;
    else if (id <= 47) state_.controls_17_47[id - 17] = value;
    else state_.controls_50_62[id - 50] = value;
    set_flags(value);
    if ((id == 19 || id == 20) && state_.binding)
        callback([&] { audio_.raw_control(id, value); });
}
void NativeHitController::bind_current() {
    const auto patch = static_cast<std::uint32_t>(state_.controls_50_62[0]);
    if (!patch) {
        state_.last_binding_outcome = NativeHitBindingOutcome::zero;
        return;
    }
    if (patch_source_.scope == NativeHitPatchSource::Scope::global_catalog) {
        // The object namespace is shared by Track and Patch. A winning Track
        // masks even a same-ID Patch in the originating HOT.
        const auto object = patch_source_.catalog->lookup_object(patch);
        if (!object) {
            state_.last_binding_outcome = NativeHitBindingOutcome::missing;
            return;
        }
        if (object->kind != HotObjectKind::patch) {
            // Model the native diagnostic's internal stop without attempting
            // an unsafe release through the Track object. The initializer
            // continues afterward and restores its active execution state.
            stop_impl();
            state_.last_binding_outcome = NativeHitBindingOutcome::wrong_kind;
            return;
        }
        const NativeHitPatchRef ref{patch, object->group, object->line, object->patch};
        const NativeHitState first_snapshot = state_;
        check_live();
        const auto effective_id26 = audio_.bind_global_patch(ref, first_snapshot);
        state_.binding = true;
        if (state_.faulted) {
            try { audio_.stop_current(); } catch (...) {}
            try { audio_.release_binding(); } catch (...) {}
            state_.binding = false;
            throw FormatError("native HIT bind callback reentered controller mutation");
        }
        state_.last_binding_outcome = NativeHitBindingOutcome::acquired;
        // This initializer-only writer stores the Patch getter's value and
        // updates flags. It does not enable public HIT writes to ID26.
        state_.controls_17_47[26 - 17] = effective_id26;
        set_flags(effective_id26);
        const NativeHitState second_snapshot = state_;
        callback([&] { audio_.apply_global_controls(second_snapshot); });
        return;
    }
    require(hot_.patches.find(patch) != hot_.patches.end(), "native HIT patch unavailable");
    check_live();
    audio_.bind(patch); // Strong exception guarantee belongs to the adapter.
    state_.binding = true;
    if (state_.faulted) {
        // The bind itself returned successfully, but it swallowed a reentrant
        // mutation error. Teardown is the only permitted post-fault effect.
        try { audio_.stop_current(); } catch (...) {}
        try { audio_.release_binding(); } catch (...) {}
        state_.binding = false;
        throw FormatError("native HIT bind callback reentered controller mutation");
    }
    state_.last_binding_outcome = NativeHitBindingOutcome::acquired;
}
void NativeHitController::release_current() {
    if (!state_.binding) return;
    check_live();
    audio_.release_binding();
    state_.binding = false;
    check_live();
}
void NativeHitController::stop_and_release() {
    if (!state_.binding) return;
    std::exception_ptr stop_error;
    try { callback([&] { audio_.stop_current(); }); }
    catch (...) { stop_error = std::current_exception(); }
    if (stop_error) {
        // The stop callback may have faulted or reentered. Teardown of its
        // already acquired binding is the sole allowed follow-up effect.
        try {
            audio_.release_binding();
            state_.binding = false;
        } catch (...) {}
        std::rethrow_exception(stop_error);
    }
    release_current();
}
void NativeHitController::stop_impl() {
    state_.active = false;
    stop_and_release();
    state_.binding = false;
    state_.fade_stop_pending = false;
    state_.controls_17_47[45 - 17] = 0;
    state_.controls_17_47[46 - 17] = 0;
    state_.controls_50_62[0] = 0;
    active_hit_ = &hit_;
    code_end_ = validate_target({&hit_, entry_});
    state_.pc = entry_;
    state_.deadline = 0;
    call_stack_.clear();
    state_.call_depth = 0;
    set_flags(0);
}
void NativeHitController::request_stop() {
    if (!state_.active) return;
    if (!audio_active()) { stop_impl(); return; }
    write(40, 0);
    write(41, 19);
    write(42, 100);
    state_.fade_stop_pending = true;
    write(45, 1);
}
void NativeHitController::advance_fade() {
    if (read(45) == 0) return;
    const auto target = read(40);
    const auto dest_raw = read(41);
    require((dest_raw >= 1 && dest_raw <= 16) || dest_raw == 19 || dest_raw == 20 ||
            dest_raw == 40 || dest_raw == 41 || dest_raw == 42 || dest_raw == 45 ||
            dest_raw == 55, "native HIT fade destination unsupported");
    const auto dest = static_cast<std::uint8_t>(dest_raw);
    const auto current = read(dest);
    auto step = read(42);
    if ((current < target) != (step > 0)) step = neg32(step);
    auto next = add32(current, step);
    const bool overshot = step > 0 ? next > target : step < 0 && next < target;
    if (step == 0 || overshot) {
        if (overshot) next = target;
        state_.controls_17_47[45 - 17] = 0;
    }
    write(dest, next);
}
void NativeHitController::pause_impl() {
    state_.paused = true;
    if (state_.active && state_.binding) callback([&] { audio_.pause_current(); });
}
void NativeHitController::resume_impl(std::uint32_t clock) {
    state_.paused = false;
    if (state_.active && state_.binding) callback([&] { audio_.resume_current(); });
    if (state_.active) state_.deadline = clock;
}
void NativeHitController::update_args_clear(const HitArguments& args) {
    state_.fade_stop_pending = false;
    state_.controls_17_47[45 - 17] = 0;
    state_.persistent[0] = args[0];
    set_flags(args[0]);
    state_.persistent[1] = args[1];
    set_flags(args[1]);
    state_.persistent[2] = args[2];
    set_flags(args[2]);
}
void NativeHitController::write_spatial(std::int32_t volume, std::int32_t pan) {
    write(19, volume);
    write(20, pan);
}
void NativeHitController::restart(const HitArguments& args, std::int32_t volume,
                                  std::int32_t pan, std::uint32_t clock) {
    // Complete preflight precedes release/rebinding. The caller already
    // resolved entry PC from HOT metadata; no chooser/provider is hidden here.
    const auto fresh = build_native_hit_controls(track_, args, volume, pan, overrides_);
    require(*fresh.locals[53 - 16] == 0, "native HIT restart requires own provider");
    const auto patch = static_cast<std::uint32_t>(*fresh.locals[50 - 16]);
    if (patch_source_.scope == NativeHitPatchSource::Scope::local_hot)
        require(!patch || hot_.patches.find(patch) != hot_.patches.end(),
                "native HIT restart patch unavailable");
    update_args_clear(args);
    for (std::uint32_t id = 17; id <= 47; ++id)
        state_.controls_17_47[id - 17] = *fresh.locals[id - 16];
    for (std::uint32_t id = 50; id <= 62; ++id)
        state_.controls_50_62[id - 50] = *fresh.locals[id - 16];
    state_.controls_17_47[19 - 17] = args[1];
    state_.controls_17_47[20 - 17] = args[2];
    state_.controls_17_47[46 - 17] = 0; // Final initializer writer runs after bind.
    stop_and_release();
    try {
        if (patch_source_.scope == NativeHitPatchSource::Scope::local_hot) {
            active_hit_ = &hit_;
            code_end_ = validate_target({&hit_, entry_});
            state_.pc = entry_;
            state_.deadline = clock;
        }
        bind_current();
        if (patch_source_.scope == NativeHitPatchSource::Scope::global_catalog) {
            // A wrong-kind diagnostic reset PC/deadline during binding. The
            // native initializer ignores the bind result and reinstates them.
            active_hit_ = &hit_;
            code_end_ = validate_target({&hit_, entry_});
            state_.pc = entry_;
            state_.deadline = clock;
        }
        state_.controls_17_47[46 - 17] = 1;
        set_flags(1);
        state_.active = true;
        pause_impl();
        write_spatial(volume, pan);
        resume_impl(clock);
        state_.native_diagnostic_stop = false;
    } catch (...) {
        if (state_.binding) try { stop_and_release(); } catch (...) {}
        throw;
    }
}

void NativeHitController::activate(std::uint32_t clock) {
    enter();
    if (ever_activated_) {
        leave();
        throw FormatError("native HIT initial activation already used");
    }
    try {
        ever_activated_ = true;
        restart(initial_args_, initial_volume_, initial_pan_, clock);
        leave();
    } catch (...) {
        state_.faulted = true;
        leave();
        throw;
    }
}

NativeHitPoll NativeHitController::poll(std::uint32_t clock) {
    enter();
    try {
        if (!state_.active) { leave(); return NativeHitPoll::inactive; }
        if (state_.paused) { leave(); return NativeHitPoll::paused; }
        advance_fade();
        if (state_.fade_stop_pending && read(45) == 0) {
            stop_impl(); leave(); return NativeHitPoll::stopped;
        }
        if (clock & 0x80000000U) {
            state_.native_diagnostic_stop = true;
            stop_impl(); leave(); return NativeHitPoll::diagnostic_stopped;
        }
        if (!state_.deadline || state_.deadline > clock) {
            leave(); return NativeHitPoll::waiting;
        }
        std::uint32_t in_poll = 0;
        while (state_.active && state_.deadline && state_.deadline <= clock) {
            require(state_.instructions < limits_.instructions_total &&
                    in_poll < limits_.instructions_per_poll,
                    "native HIT instruction budget exhausted");
            require(trace_.size() < limits_.trace_entries, "native HIT trace budget exhausted");
            const auto start = state_.pc;
            const auto* source = active_hit_;
            const auto op = byte();
            ++state_.instructions;
            ++in_poll;
            switch (op) {
            case 2: {
                const auto literal = byte();
                if (state_.binding) callback([&] { audio_.note_on(literal); });
                break;
            }
            case 5: {
                const auto id = byte();
                const auto value = signed_bits(word());
                write(id, value);
                break;
            }
            case 7: {
                const auto key = word();
                require(static_cast<bool>(call_resolver_) || static_cast<bool>(resource_resolver_),
                        "native HIT Call7 requires a caller-supplied resolver");
                check_live();
                std::optional<HitCodeTarget> target;
                if (resource_resolver_) target = resource_resolver_(key);
                else if (const auto local = call_resolver_(key))
                    target = HitCodeTarget{active_hit_, *local};
                check_live(); // A resolver may swallow a reentrant mutation error.
                require(target.has_value(), "native HIT Call7 key is unresolved");
                const auto target_end = validate_target(*target);
                require(call_stack_.size() < limits_.call_depth,
                        "native HIT call depth exhausted");
                call_stack_.push_back({active_hit_, state_.pc});
                state_.call_depth = static_cast<std::uint32_t>(call_stack_.size());
                active_hit_ = target->file;
                code_end_ = target_end;
                state_.pc = target->pc;
                break;
            }
            case 8: case 12:
                if (call_stack_.empty()) request_stop();
                else {
                    const auto frame = call_stack_.back();
                    call_stack_.pop_back();
                    active_hit_ = frame.file;
                    // Return addresses are allowed to equal the code end;
                    // the next byte fetch checks their validity.
                    code_end_ = static_cast<std::uint32_t>(
                        frame.file->entry_table_offset.value_or(frame.file->bytes.size()));
                    state_.pc = frame.pc;
                    state_.call_depth = static_cast<std::uint32_t>(call_stack_.size());
                }
                break;
            case 9: {
                const auto value = read(byte());
                state_.deadline = clock + static_cast<std::uint32_t>(value);
                break;
            }
            case 11:
                if (audio_active()) {
                    state_.pc = start;
                    state_.deadline = clock + 2U;
                }
                break;
            case 27: request_stop(); break;
            default: throw FormatError("unsupported native HIT opcode " + std::to_string(op));
            }
            trace_.push_back({start, state_.pc, op, source, active_hit_});
            if (in_poll == 102) {
                state_.native_diagnostic_stop = true;
                stop_impl();
                leave();
                return NativeHitPoll::diagnostic_stopped;
            }
        }
        const auto result = state_.active ? (in_poll ? NativeHitPoll::progressed : NativeHitPoll::waiting)
                                          : NativeHitPoll::stopped;
        leave();
        return result;
    } catch (...) {
        state_.faulted = true;
        leave();
        throw;
    }
}

void NativeHitController::pause() {
    enter();
    try {
        pause_impl();
        leave();
    } catch (...) { state_.faulted = true; leave(); throw; }
}
void NativeHitController::resume(std::uint32_t clock) {
    enter();
    if (!state_.active) {
        leave();
        throw FormatError("native HIT resume requires active controller");
    }
    try {
        resume_impl(clock);
        leave();
    } catch (...) { state_.faulted = true; leave(); throw; }
}
void NativeHitController::retrigger(const HitArguments& args, std::int32_t volume,
                                    std::int32_t pan, std::uint32_t clock) {
    enter();
    if (!ever_activated_) {
        leave();
        throw FormatError("native HIT retrigger requires prior activation");
    }
    try {
        require(read(53) == 0, "native HIT shared provider unsupported");
        if (state_.active && state_.paused) {
            update_args_clear(args);
            resume_impl(clock); // initializer +30 resumes before caller setters
            write_spatial(volume, pan);
            resume_impl(clock); // caller type1 activates again
        } else if (state_.active && read(55) == 0) {
            update_args_clear(args);
            write_spatial(volume, pan);
            resume_impl(clock);
        } else {
            restart(args, volume, pan, clock);
        }
        leave();
    } catch (...) { state_.faulted = true; leave(); throw; }
}
void NativeHitController::stop() {
    enter();
    try { stop_impl(); state_.native_diagnostic_stop = false; leave(); }
    catch (...) { state_.faulted = true; leave(); throw; }
}
} // namespace odh
