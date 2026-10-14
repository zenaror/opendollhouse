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
    require(kills == 1, "group request actually applied once");
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
