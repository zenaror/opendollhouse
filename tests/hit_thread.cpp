#include "odh/hit_thread.hpp"
#include <iostream>

namespace {
int checks = 0;
void require(bool value, const char* why) {
    ++checks;
    if (!value) throw std::runtime_error(why);
}
template<class F> void rejects(F action) {
    bool failed = false;
    try { action(); } catch (const odh::FormatError&) { failed = true; }
    require(failed, "expected explicit HIT failure");
}
struct Fixture {
    odh::HitFile hit;
    odh::Hot hot;
    std::array<std::int32_t,29> vars{};
    Fixture(std::initializer_list<std::uint8_t> code) {
        hit.major_version = 1; hit.bytes.resize(16);
        hit.bytes.insert(hit.bytes.end(),code);
        hot.patches[100] = {"synthetic", "synthetic.xa", false, false};
    }
};
}
int main() { try {
    // LoadL patch100, NoteOn reg5, WaitSamp, End.
    Fixture f{5,50,100,0,0,0,2,5,11,12};
    int starts = 0, queries = 0;
    auto status = odh::HitNoteStatus::active;
    odh::HitNoteBackend backend;
    backend.start = [&](std::uint32_t patch)->std::optional<std::uint64_t> {
        require(patch == 100, "selected patch supplied to backend"); ++starts; return 42;
    };
    backend.status = [&](std::uint64_t handle) {
        require(handle == 42, "stable owned handle queried"); ++queries; return status;
    };
    odh::HitThread t(f.hit,f.hot,16,f.vars,{},backend);
    require(!t.state().zero_flag && !t.state().sign_flag && !t.state().initial_controls,
            "legacy thread starts with clear flags and no native receipt");
    require(t.tick() == odh::HitThreadTick::yielded && t.pc() == 24,
            "active WaitSamp rewinds PC to opcode and yields");
    require(starts == 1 && t.state().registers[5] == 0 && t.notes_started() == 1,
            "NoteOn writes sequential note index exactly once");
    require(t.tick() == odh::HitThreadTick::yielded && starts == 1 && t.pc() == 24,
            "waiting tick does not resubmit audio");
    status = odh::HitNoteStatus::complete;
    require(t.tick() == odh::HitThreadTick::yielded && t.pc() == 25 && !t.bytecode_ended(),
            "completed WaitSamp advances but still yields a tick");
    require(t.tick() == odh::HitThreadTick::yielded && t.bytecode_ended(),
            "End marks bytecode ended and retains thread for next tick");
    require(t.tick() == odh::HitThreadTick::complete && !t.faulted(), "thread completes after note");
    const auto old_queries = queries;
    require(t.tick() == odh::HitThreadTick::complete && queries == old_queries,
            "terminal tick idempotent without backend queries");
    // End without WaitSamp must also preserve live audio.
    Fixture e{5,50,100,0,0,0,2,5,12};
    status = odh::HitNoteStatus::active;
    odh::HitThread ending(e.hit,e.hot,16,e.vars,{},backend);
    require(ending.tick() == odh::HitThreadTick::yielded && ending.bytecode_ended(), "End yields");
    require(ending.tick() == odh::HitThreadTick::yielded, "End does not terminate active note");
    status = odh::HitNoteStatus::complete;
    require(ending.tick() == odh::HitThreadTick::complete, "End drains last note");
    // Failed start writes -1 and WaitSamp with no successful note still yields.
    auto no_start = backend;
    no_start.start = [](auto)->std::optional<std::uint64_t> { return {}; };
    no_start.status = [](auto)->odh::HitNoteStatus { throw std::runtime_error("unexpected query"); };
    odh::HitThread failed_start(f.hit,f.hot,16,f.vars,{},no_start);
    require(failed_start.tick() == odh::HitThreadTick::yielded && failed_start.pc() == 25 &&
            failed_start.state().registers[5] == -1 && failed_start.notes_started() == 0,
            "failed start distinct from successful note");
    failed_start.tick();
    require(failed_start.tick() == odh::HitThreadTick::complete, "empty notes complete after End");
    // A failed second start leaves last successful note controlling WaitSamp.
    Fixture multi{5,50,100,0,0,0,2,5,2,6,11,12};
    unsigned attempts = 0;
    auto partial = backend;
    partial.start = [&](auto)->std::optional<std::uint64_t> { return ++attempts == 1 ? std::optional<std::uint64_t>{42} : std::nullopt; };
    status = odh::HitNoteStatus::active;
    odh::HitThread second(multi.hit,multi.hot,16,multi.vars,{},partial);
    second.tick();
    require(second.pc() == 26 && second.state().registers[6] == -1 && second.notes_started() == 1,
            "failed latest attempt does not replace LastNote");
    // Explicit backend failure latches and cannot retry a note.
    status = odh::HitNoteStatus::failed;
    rejects([&] { second.tick(); });
    require(second.faulted(), "backend failure latched");
    rejects([&] { second.tick(); });
    require(attempts == 2, "faulted thread cannot replay effects");
    // Lifetime budgets include repeated waits and successful notes.
    status = odh::HitNoteStatus::active;
    odh::HitThreadLimits limits; limits.execution.instructions = 3;
    odh::HitThread budget(f.hit,f.hot,16,f.vars,{},backend,limits);
    budget.tick(); rejects([&] { budget.tick(); });
    limits = {}; limits.notes = 1;
    odh::HitThread notes(multi.hit,multi.hot,16,multi.vars,{},backend,limits);
    rejects([&] { notes.tick(); });
    require(notes.notes_started() == 1, "note limit enforced before second start");
    Fixture group{69,7,12}; unsigned kills = 0;
    odh::HitThread missing(group.hit,group.hot,16,group.vars,{},backend);
    rejects([&] { missing.tick(); });
    auto groups = backend; groups.kill_group = [&](std::uint8_t value) { require(value == 7,"group operand retained"); ++kills; };
    odh::HitThread grouped(group.hit,group.hot,16,group.vars,{},groups);
    grouped.tick(); grouped.tick();
    require(kills == 1 && grouped.state().sequence_group_kills == std::vector<std::uint8_t>({7}) &&
            grouped.state().trace[0].sequence_group_kill == 7,
            "legacy group request applies once and retains its trace");
    odh::HotTrack group_track; group_track.hitlist_id = 0; group_track.patch_id = 100;
    group_track.arg_type = 2; group_track.volume = 1024; group_track.ducking_priority = 0;
    const odh::HitArguments group_raw{{1, 0, 0}};
    const auto group_controls = odh::build_native_hit_controls(group_track, group_raw, 1024, 512);
    int native_group_effects = 0;
    auto native_groups = backend;
    native_groups.kill_group = [&](std::uint8_t) { ++native_group_effects; };
    for (const std::uint8_t operand : {std::uint8_t{0}, std::uint8_t{1}}) {
        Fixture native_group{69, operand, 12};
        odh::HitThread native(native_group.hit, native_group.hot, 16, native_group.vars,
                              {}, native_groups, {}, &group_track, odh::HitJumpOperand::FreeSoHeuristic,
                              {}, group_raw, group_controls);
        bool refused = false;
        try { native.tick(); }
        catch (const odh::FormatError& error) {
            const std::string message = error.what();
            refused = message.find("native opcode 69 requires controller registry") != std::string::npos &&
                      message.find("cursor 18") != std::string::npos;
        }
        require(refused && native.faulted() && native.pc() == 18 && native.state().steps == 1 &&
                native.state().trace.empty() && native.state().sequence_group_kills.empty() &&
                native_group_effects == 0,
                "native opcode69 rejects after operand without invoking legacy callback or recording effect");
        rejects([&] { native.tick(); });
        require(native.pc() == 18 && native_group_effects == 0,
                "native opcode69 fault is latched without callback retry or cursor advance");
    }
    Fixture truncated_group{69};
    odh::HitThread truncated_native(truncated_group.hit, truncated_group.hot, 16,
                                    truncated_group.vars, {}, native_groups, {}, &group_track,
                                    odh::HitJumpOperand::FreeSoHeuristic, {}, group_raw, group_controls);
    bool missing_operand = false;
    try { truncated_native.tick(); }
    catch (const odh::FormatError& error) {
        const std::string message = error.what();
        missing_operand = message.find("address outside HIT code region") != std::string::npos &&
                          message.find("cursor 17") != std::string::npos;
    }
    require(missing_operand && truncated_native.faulted() && truncated_native.pc() == 17 &&
            native_group_effects == 0,
            "truncated native opcode69 faults on missing operand before native gate or callback");
    Fixture unsupported{7};
    odh::HitThread unknown(unsupported.hit,unsupported.hot,16,unsupported.vars,{},backend);
    rejects([&] { unknown.tick(); });
    f.hot.patches[100].looped = true;
    odh::HitThread loop(f.hit,f.hot,16,f.vars,{},backend);
    rejects([&] { loop.tick(); });
    rejects([&] { odh::HitThread absent(e.hit,e.hot,16,e.vars,{},{}); });
    f.hot.patches[100].looped = false;
    odh::HitThread alias(multi.hit,multi.hot,16,multi.vars,{},backend);
    rejects([&] { alias.tick(); });
    require(alias.notes_started() == 1, "duplicate backend handle rejected");
    auto invalid_status = backend;
    invalid_status.status = [](auto) { return static_cast<odh::HitNoteStatus>(99); };
    odh::HitThread invalid(f.hit,f.hot,16,f.vars,{},invalid_status);
    rejects([&] { invalid.tick(); });
    Fixture truncated{5,50,100,0,0,0,2};
    const auto before_starts = starts;
    odh::HitThread trunc(truncated.hit,truncated.hot,16,truncated.vars,{},backend);
    rejects([&] { trunc.tick(); });
    require(starts == before_starts, "truncated NoteOn rejected before callback");
    limits = {}; limits.execution.trace_entries = 1;
    odh::HitThread trace(e.hit,e.hot,16,e.vars,{},backend,limits);
    rejects([&] { trace.tick(); });
    require(starts == before_starts, "trace budget checked before note effect");
    {
        Fixture native{14, 19, 5, 18, 200, 0, 0, 0, 2, 5, 12};
        native.hot.patches[200] = {"other", "other.xa", false, false};
        native.hot.hitlists[77].ids = {100};
        odh::HotTrack track;
        track.hitlist_id = 77; track.patch_id = 0; track.arg_type = 2;
        track.volume = 700; track.ducking_priority = 3;
        const odh::HitArguments raw{{5, 0, 0}};
        const auto controls = odh::build_native_hit_controls(track, raw, 900, 500);
        int native_choices = 0, native_starts = 0;
        auto native_backend = backend;
        native_backend.start = [&](std::uint32_t patch)->std::optional<std::uint64_t> {
            require(patch == 100, "script write18 cannot switch selected native patch");
            ++native_starts; return 314;
        };
        native_backend.status = [](std::uint64_t) { return odh::HitNoteStatus::complete; };
        odh::HitThread thread(native.hit, native.hot, 16, native.vars,
            [&](std::uint32_t list, const auto& ids) {
                require(list == 77 && ids == std::vector<std::uint32_t>({100}),
                        "native initial track invokes correct chooser");
                ++native_choices; return 0U;
            }, native_backend, {}, &track, odh::HitJumpOperand::FreeSoHeuristic, {}, raw, controls);
        require(native_choices == 1 && native_starts == 0 && thread.state().steps == 0 &&
                thread.state().trace.empty() && !thread.state().zero_flag && !thread.state().sign_flag &&
                thread.state().initial_controls->locals[18 - 16] == 700 &&
                thread.state().initial_controls->locals[19 - 16] == 900 &&
                thread.state().initial_controls->locals[25 - 16] == 3 &&
                thread.state().initial_controls->locals[50 - 16] == 0 &&
                thread.state().patch_id == 0,
                "native state and one initial choice exist before first tick without script effects");
        require(thread.tick() == odh::HitThreadTick::yielded && native_choices == 1 &&
                native_starts == 1 && thread.state().registers[1] == 5 &&
                thread.state().registers[2] == 0 && thread.state().registers[3] == 0 &&
                thread.state().trace[0].opcode == 14 &&
                thread.state().locals[18 - 16] == 200 &&
                thread.state().initial_controls->locals[18 - 16] == 700,
                "first native tick reads post-spatial controls; receipt stays immutable");
        thread.tick();
        require(thread.tick() == odh::HitThreadTick::complete && native_choices == 1,
                "native startup does not reselect track on later ticks");
    }
    for (const std::int32_t pan : {-512, 0, 512}) {
        Fixture native{12};
        odh::HotTrack track; track.hitlist_id = 0; track.patch_id = 100;
        track.arg_type = 2; track.volume = 1024; track.ducking_priority = 0;
        const odh::HitArguments raw{{1, 0, 0}};
        auto controls = odh::build_native_hit_controls(track, raw, 1024, pan);
        odh::HitThread thread(native.hit, native.hot, 16, native.vars, {}, backend,
                              {}, &track, odh::HitJumpOperand::FreeSoHeuristic, {}, raw, controls);
        require(thread.state().steps == 0 && thread.state().trace.empty() &&
                thread.state().zero_flag == (pan == 0) &&
                thread.state().sign_flag == (pan < 0) &&
                thread.state().initial_controls->initial_zero_flag == (pan == 0) &&
                thread.state().initial_controls->initial_sign_flag == (pan < 0),
                "native thread exposes final-pan flags in receipt before first tick");
    }
    // Reentrancy cannot be suppressed by a callback to continue effects.
    odh::HitThread* self = nullptr;
    auto recursive = backend;
    recursive.start = [&](auto)->std::optional<std::uint64_t> {
        rejects([&] { self->tick(); }); return 99;
    };
    odh::HitThread reentrant(multi.hit,multi.hot,16,multi.vars,{},recursive);
    self = &reentrant;
    rejects([&] { reentrant.tick(); });
    require(reentrant.faulted() && reentrant.notes_started() == 0,
            "swallowed recursive failure stops outer execution immediately");
    std::cout << checks << " HIT thread lifecycle checks passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; } }
