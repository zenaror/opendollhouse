#include "odh/hit_native_controller.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F&& fn) {
    bool rejected = false;
    try { fn(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, "expected native HIT FormatError");
}
struct Fixture {
    odh::HitFile hit;
    odh::Hot hot;
    odh::HotTrack track;
    explicit Fixture(std::initializer_list<std::uint8_t> code, std::uint32_t patch = 100) {
        hit.major_version = 1;
        hit.bytes.resize(16);
        hit.bytes.insert(hit.bytes.end(), code);
        hot.patches[100] = {"synthetic", "synthetic.xa", false, false};
        track.hitlist_id = 0;
        track.patch_id = patch;
        track.arg_type = 2;
        track.volume = 1024;
        track.ducking_priority = 0;
        track.sound_pressure_level = 20;
    }
};
struct Audio {
    std::vector<std::string> events;
    bool active = false;
    odh::NativeHitAudio callbacks() {
        return {
            [&](std::uint32_t patch) { events.push_back("bind:" + std::to_string(patch)); },
            [&] { events.push_back("active"); return active; },
            [&](std::uint8_t value) { events.push_back("note:" + std::to_string(value)); },
            [&] { events.push_back("stop"); },
            [&] { events.push_back("release"); },
            [&] { events.push_back("pause"); },
            [&] { events.push_back("resume"); },
            [&](std::uint8_t id, std::int32_t value) {
                events.push_back("raw:" + std::to_string(id) + ":" + std::to_string(value));
            }
        };
    }
    unsigned count(const std::string& key) const {
        return static_cast<unsigned>(std::count(events.begin(), events.end(), key));
    }
};
odh::HitArguments args(std::int32_t a = 1, std::int32_t b = 2, std::int32_t c = 3) {
    return {{a, b, c}};
}
}

int main() {
try {
    // The interpreter consumes a real byte stream: LoadL writes persistent
    // ID16, NoteOn uses the literal, active WaitSamp retries, inactive WaitSamp
    // continues through Wait9 and End can execute following bytecode same poll.
    Fixture f{5,16,7,0,0,0, 2,9, 11, 5,4,2,0,0,0, 9,4,
              12, 5,6,1,0,0,0, 9,6, 9,6};
    Audio a;
    odh::NativeHitController c(f.hit, f.hot, f.track, 16, args(), 150, 512, a.callbacks());
    require(c.state().persistent[15] == 0 && !c.state().active && c.state().paused,
            "constructor prepares fresh inactive state without audio effects");
    require(a.events.empty(), "constructor has no audio effects");
    c.activate(1);
    require(c.state().active && !c.state().paused && c.state().deadline == 1,
            "activation copies native Clock");
    a.active = true;
    require(c.poll(1) == odh::NativeHitPoll::progressed && c.state().deadline == 3 &&
            c.state().pc == 24 && c.state().persistent[15] == 7,
            "active WaitSamp retries opcode with Clock plus two");
    require(c.trace().size() == 3 && c.trace()[0].opcode == 5 &&
            c.trace()[1].opcode == 2 && c.trace()[2].opcode == 11 &&
            c.trace()[2].pc == c.trace()[2].next_pc,
            "trace records decoded bytecode and active WaitSamp retry PC");
    require(a.count("note:9") == 1 && c.state().persistent[8] == 0,
            "NoteOn passes literal without writing a note handle into ID9");
    require(c.poll(2) == odh::NativeHitPoll::waiting, "deadline blocks instruction before Clock3");
    a.active = false;
    require(c.poll(3) == odh::NativeHitPoll::progressed && c.state().deadline == 5 &&
            c.state().pc == 33 && c.state().persistent[3] == 2,
            "inactive WaitSamp continues same poll and Wait9 reads variable ID4");
    require(c.poll(4) == odh::NativeHitPoll::waiting, "Wait9 deadline is absolute Clock plus variable");
    a.active = true;
    require(c.poll(5) == odh::NativeHitPoll::progressed && c.state().fade_stop_pending &&
            c.state().controls_17_47[45-17] == 1 && c.state().deadline == 6 &&
            c.state().persistent[5] == 1 && c.state().positive,
            "End arms fade then executes following LoadL and Wait9 in same poll");
    require(c.poll(6) == odh::NativeHitPoll::progressed &&
            c.state().controls_17_47[19-17] == 50 && c.state().deadline == 7,
            "fade advances before due bytecode");
    require(c.poll(7) == odh::NativeHitPoll::stopped && !c.state().active &&
            c.state().persistent[15] == 7 && c.state().controls_50_62[0] == 0,
            "strict fade overshoot stops and retains persistent bank");
    require(a.count("stop") == 1 && a.count("release") == 1,
            "stop and binding teardown are distinct callbacks");

    Fixture no_binding{2,3,11,12,5,1,9,0,0,0, 9,1}, zero_patch = no_binding;
    zero_patch.track.patch_id = 0;
    Audio absent;
    odh::NativeHitController unbound(zero_patch.hit, zero_patch.hot, zero_patch.track,
                                     16, args(), 100, 512, absent.callbacks());
    unbound.activate(1);
    require(unbound.poll(1) == odh::NativeHitPoll::stopped && absent.count("note:3") == 0 &&
            absent.count("bind:100") == 0 && absent.count("release") == 0,
            "patch zero has no ghost binding, NoteOn handle or WaitSamp query");

    Fixture paused{5,4,5,0,0,0,9,4,27};
    Audio p;
    odh::NativeHitController q(paused.hit, paused.hot, paused.track,16,args(),100,512,p.callbacks());
    q.activate(1);
    require(q.poll(1) == odh::NativeHitPoll::progressed && q.state().deadline == 6,
            "Wait9 schedules by variable value");
    q.pause();
    require(q.poll(6) == odh::NativeHitPoll::paused && q.state().deadline == 6,
            "pause retains deadline and PC");
    q.resume(4);
    require(q.state().deadline == 4 && q.poll(4) == odh::NativeHitPoll::stopped,
            "resume replaces deadline with current Clock, then opcode27 stops");
    require(p.count("pause") == 2 && p.count("resume") == 2,
            "initializer pause and explicit pause each reach bound audio");
    rejects([&] { q.resume(5); });
    require(!q.state().faulted, "inactive resume precondition rejects without execution fault");

    Fixture restart{5,16,77,0,0,0,5,4,2,0,0,0,5,55,1,0,0,0,9,4};
    Audio r;
    odh::NativeHitController x(restart.hit,restart.hot,restart.track,16,args(),100,512,r.callbacks());
    x.activate(1);
    x.poll(1);
    require(x.state().persistent[15] == 77 && x.state().controls_50_62[55-50] == 1,
            "bytecode writes ID16 and interrupt55");
    const auto traced = x.trace().size();
    x.retrigger(args(8,9,10),101,513,4);
    require(x.state().pc == 16 && x.state().deadline == 4 &&
            x.state().persistent[15] == 77 && x.state().persistent[3] == 2 &&
            x.state().controls_50_62[55-50] == 0 && x.trace().size() == traced,
            "restart preserves R4-R16 and trace but rebuilds fresh control banks");
    require(r.count("bind:100") == 2 && r.count("release") == 1,
            "restart tears down and rebinds even the same patch");
    x.stop();
    require(x.state().persistent[15] == 77 && x.state().controls_50_62[0] == 0,
            "stop leaves persistent bank and clears current patch binding");
    x.retrigger(args(11),110,500,8);
    require(x.state().active && x.state().persistent[15] == 77 &&
            r.count("bind:100") == 3, "inactive retrigger restarts same controller");

    Fixture active_update{5,4,2,0,0,0,9,4};
    Audio u;
    odh::NativeHitController y(active_update.hit,active_update.hot,active_update.track,
                               16,args(),100,512,u.callbacks());
    y.activate(1); y.poll(1);
    const auto before_pc = y.state().pc;
    y.retrigger(args(20,21,22),111,600,3);
    require(y.state().pc == before_pc && y.state().deadline == 3 &&
            y.state().persistent[0] == 20 && y.state().controls_17_47[20-17] == 600 &&
            u.count("bind:100") == 1,
            "active interrupt-clear retrigger updates raw controls and activation Clock without rebind");
    rejects([&] { y.activate(3); });
    require(!y.state().faulted, "second initial activation precondition does not fault controller");
    y.pause();
    y.retrigger(args(30),112,601,4);
    require(!y.state().paused && y.state().pc == before_pc &&
            y.state().deadline == 4 && u.count("bind:100") == 1,
            "paused retrigger resumes before interrupt check");

    std::vector<std::uint8_t> long_code(103 * 6, 0);
    for (std::size_t n = 0; n < 103; ++n) {
        long_code[n*6] = 5;
        long_code[n*6+1] = 1;
        long_code[n*6+2] = 1;
    }
    Fixture diagnostic{};
    diagnostic.hit.major_version = 1;
    diagnostic.hit.bytes.resize(16);
    diagnostic.hit.bytes.insert(diagnostic.hit.bytes.end(),long_code.begin(),long_code.end());
    diagnostic.hot.patches[100] = {"synthetic", "synthetic.xa",false,false};
    diagnostic.track = restart.track;
    Audio d;
    odh::NativeHitController z(diagnostic.hit,diagnostic.hot,diagnostic.track,
                               16,args(),100,512,d.callbacks());
    z.activate(1);
    require(z.poll(1) == odh::NativeHitPoll::diagnostic_stopped &&
            z.state().instructions == 102 && z.trace().size() == 102 &&
            z.state().native_diagnostic_stop && !z.state().faulted,
            "native post-102 instruction diagnostic takes default stop path");

    Audio h;
    odh::NativeHitController high(paused.hit,paused.hot,paused.track,16,args(),100,512,h.callbacks());
    high.activate(0x80000000U);
    require(high.poll(0x80000000U) == odh::NativeHitPoll::diagnostic_stopped &&
            high.state().native_diagnostic_stop && !high.state().faulted &&
            high.state().instructions == 0,
            "high-bit Clock uses native diagnostic stop before bytecode");
    Audio max_clock;
    odh::NativeHitController near_sign(paused.hit,paused.hot,paused.track,
                                       16,args(),100,512,max_clock.callbacks());
    near_sign.activate(0x7fffffffU);
    require(near_sign.poll(0x7fffffffU) == odh::NativeHitPoll::progressed &&
            near_sign.state().deadline == 0x80000004U && !near_sign.state().faulted,
            "Wait9 ADD32 retains high-bit deadline at maximum low-bit Clock");
    Audio zero;
    odh::NativeHitController at_zero(paused.hit,paused.hot,paused.track,
                                     16,args(),100,512,zero.callbacks());
    at_zero.activate(0);
    require(at_zero.poll(1) == odh::NativeHitPoll::waiting &&
            at_zero.state().instructions == 0,
            "Clock zero activation leaves deadline zero, even after Clock advances");

    Fixture zero_wait{5,4,0,0,0,0,9,4,27};
    Audio zw;
    odh::NativeHitController same_poll(zero_wait.hit,zero_wait.hot,zero_wait.track,
                                       16,args(),100,512,zw.callbacks());
    same_poll.activate(1);
    require(same_poll.poll(1) == odh::NativeHitPoll::stopped &&
            same_poll.trace().size() == 3,
            "Wait9 value zero leaves deadline at Clock and executes next opcode same poll");
    Fixture wrapped{5,4,255,255,255,255,9,4,27};
    Audio wrap_audio;
    odh::NativeHitController wrap(wrapped.hit,wrapped.hot,wrapped.track,
                                  16,args(),100,512,wrap_audio.callbacks());
    wrap.activate(1);
    require(wrap.poll(1) == odh::NativeHitPoll::progressed &&
            wrap.state().deadline == 0 && wrap.state().active,
            "Wait9 ADD32 wrap to zero disables bytecode without clamping");
    require(wrap.poll(2) == odh::NativeHitPoll::waiting,
            "zero wrapped deadline remains disabled on later low-bit Clock");

    Fixture fade_flags{5,4,255,255,255,255,12,9,4};
    Audio ff; ff.active = true;
    odh::NativeHitController end_flags(fade_flags.hit,fade_flags.hot,fade_flags.track,
                                       16,args(),100,512,ff.callbacks());
    end_flags.activate(1);
    require(end_flags.poll(1) == odh::NativeHitPoll::progressed &&
            end_flags.state().fade_stop_pending && end_flags.state().deadline == 0 &&
            !end_flags.state().zero && end_flags.state().positive && !end_flags.state().sign,
            "End public writes leave flags from final ID45=1 and do not yield");

    Fixture bad{7,0,0,0,0};
    Audio b;
    odh::NativeHitController unsupported(bad.hit,bad.hot,bad.track,16,args(),100,512,b.callbacks());
    unsupported.activate(1);
    rejects([&] { unsupported.poll(1); });
    require(unsupported.state().faulted && unsupported.state().call_depth == 0,
            "Call7 without resolver faults without inventing a target");
    const auto effect_count = b.events.size();
    rejects([&] { unsupported.stop(); });
    require(b.events.size() == effect_count, "fault prevents later backend effects");

    // The DWORDs are lookup keys, deliberately different from the bytecode
    // offsets 28 and 36. Both native return opcodes pop the same LIFO stack.
    Fixture nested{7,0x12,0x34,0x56,0x78, 2,3, 12,
                   0,0,0,0,
                   7,0xdd,0xcc,0xbb,0xaa, 2,2, 8,
                   2,1, 12};
    Audio na;
    std::vector<std::uint32_t> keys;
    odh::NativeHitCallResolver resolve_nested = [&](std::uint32_t key)
            -> std::optional<std::uint32_t> {
        keys.push_back(key);
        if (key == 0x78563412U) return 28;
        if (key == 0xaabbccddU) return 36;
        return std::nullopt;
    };
    odh::NativeHitController nc(nested.hit,nested.hot,nested.track,16,args(),100,512,
                                na.callbacks(),{}, {},resolve_nested);
    nc.activate(1);
    require(nc.poll(1) == odh::NativeHitPoll::stopped && !nc.state().faulted &&
            nc.state().call_depth == 0 && nc.trace().size() == 8,
            "nested Call7 and both return opcodes finish through LIFO stack");
    require(keys == std::vector<std::uint32_t>({0x78563412U,0xaabbccddU}) &&
            na.count("note:1") == 1 && na.count("note:2") == 1 &&
            na.count("note:3") == 1 &&
            nc.trace()[0].next_pc == 28 && nc.trace()[1].next_pc == 36 &&
            nc.trace()[3].next_pc == 33 && nc.trace()[5].next_pc == 21,
            "Call7 resolves full DWORD keys and returns to post-operand PCs");

    Fixture pending_call{7,1,0,0,0, 8, 0,0,0,0,0,0,
                         5,55,1,0,0,0, 5,4,2,0,0,0, 9,4, 8};
    Audio pa;
    bool resolve_to_return = false;
    odh::NativeHitController pending(pending_call.hit,pending_call.hot,pending_call.track,
        16,args(),100,512,pa.callbacks(),{}, {},
        [&](std::uint32_t key) -> std::optional<std::uint32_t> {
            if (key == 1) return resolve_to_return ? 42U : 28U;
            return std::nullopt;
        });
    pending.activate(1);
    require(pending.poll(1) == odh::NativeHitPoll::progressed &&
            pending.state().call_depth == 1 && pending.state().pc == 42,
            "wait inside callee retains return cursor");
    pending.pause();
    pending.retrigger(args(),100,512,2);
    require(pending.state().call_depth == 1 && pending.state().pc == 42,
            "paused retrigger keeps call stack and execution PC");
    pending.retrigger(args(),100,512,3);
    require(pending.state().call_depth == 1 && pending.state().pc == 16,
            "active ID55 restart resets PC but retains return stack");
    resolve_to_return = true;
    require(pending.poll(3) == odh::NativeHitPoll::stopped &&
            pending.state().call_depth == 0 && pending.trace().size() == 8 &&
            pending.trace()[5].opcode == 8 && pending.trace()[6].opcode == 8,
            "return after restart consumes the inherited frame after the new frame");
    pending.stop();
    require(pending.state().call_depth == 0 && pending.state().pc == 16,
            "stop clears saved return cursors");

    Fixture absent_key{7,9,0,0,0};
    Audio ak;
    odh::NativeHitController missing_key(absent_key.hit,absent_key.hot,absent_key.track,
        16,args(),100,512,ak.callbacks(),{}, {},
        [](std::uint32_t) -> std::optional<std::uint32_t> { return std::nullopt; });
    missing_key.activate(1);
    rejects([&] { missing_key.poll(1); });
    require(missing_key.state().faulted && missing_key.state().call_depth == 0,
            "missing resolver key faults before pushing a return cursor");
    Audio ia;
    odh::NativeHitController invalid_target(absent_key.hit,absent_key.hot,absent_key.track,
        16,args(),100,512,ia.callbacks(),{}, {},
        [](std::uint32_t) -> std::optional<std::uint32_t> { return 15; });
    invalid_target.activate(1);
    rejects([&] { invalid_target.poll(1); });
    require(invalid_target.state().faulted && invalid_target.state().call_depth == 0,
            "out-of-region Call7 target faults before push");
    Fixture truncated_call{7,1,2,3};
    Audio ta;
    unsigned lookups = 0;
    odh::NativeHitController truncated_target(truncated_call.hit,truncated_call.hot,
        truncated_call.track,16,args(),100,512,ta.callbacks(),{}, {},
        [&](std::uint32_t) -> std::optional<std::uint32_t> { ++lookups; return 16; });
    truncated_target.activate(1);
    rejects([&] { truncated_target.poll(1); });
    require(truncated_target.state().faulted && lookups == 0 &&
            truncated_target.state().call_depth == 0,
            "truncated Call7 operand faults before resolver invocation");

    Fixture recursive{7,1,0,0,0};
    Audio ra;
    odh::NativeHitLimits shallow;
    shallow.call_depth = 2;
    odh::NativeHitController bounded_call(recursive.hit,recursive.hot,recursive.track,
        16,args(),100,512,ra.callbacks(),shallow, {},
        [](std::uint32_t) -> std::optional<std::uint32_t> { return 16; });
    bounded_call.activate(1);
    rejects([&] { bounded_call.poll(1); });
    require(bounded_call.state().faulted && bounded_call.state().call_depth == 2 &&
            bounded_call.trace().size() == 2,
            "recursive Call7 faults at preallocated depth budget");
    Audio da;
    odh::NativeHitLimits diagnostic_depth;
    diagnostic_depth.call_depth = 128;
    odh::NativeHitController diagnostic_call(recursive.hit,recursive.hot,recursive.track,
        16,args(),100,512,da.callbacks(),diagnostic_depth, {},
        [](std::uint32_t) -> std::optional<std::uint32_t> { return 16; });
    diagnostic_call.activate(1);
    require(diagnostic_call.poll(1) == odh::NativeHitPoll::diagnostic_stopped &&
            diagnostic_call.state().instructions == 102 &&
            diagnostic_call.state().call_depth == 0 &&
            !diagnostic_call.state().faulted,
            "native post-102 diagnostic stop clears pending call frames");
    shallow.call_depth = 0;
    rejects([&] {
        odh::NativeHitController invalid_limit(recursive.hit,recursive.hot,recursive.track,
            16,args(),100,512,ra.callbacks(),shallow);
    });

    Audio resolver_audio;
    odh::NativeHitController* resolver_ptr = nullptr;
    odh::NativeHitController resolver_reentry(absent_key.hit,absent_key.hot,absent_key.track,
        16,args(),100,512,resolver_audio.callbacks(),{}, {},
        [&](std::uint32_t) -> std::optional<std::uint32_t> {
            try { resolver_ptr->pause(); } catch (const odh::FormatError&) {}
            return 16;
        });
    resolver_ptr = &resolver_reentry;
    resolver_reentry.activate(1);
    rejects([&] { resolver_reentry.poll(1); });
    require(resolver_reentry.state().faulted && resolver_reentry.state().call_depth == 0 &&
            resolver_reentry.trace().empty(),
            "swallowed resolver reentry faults before changing call stack or PC");

    Fixture callback_code{2,5,9,4};
    Audio cb;
    odh::NativeHitController* ptr = nullptr;
    auto funcs = cb.callbacks();
    funcs.note_on = [&](std::uint8_t) {
        try { ptr->pause(); } catch (const odh::FormatError&) {}
    };
    odh::NativeHitController reentrant(callback_code.hit,callback_code.hot,
        callback_code.track,16,args(),100,512,funcs);
    ptr = &reentrant;
    reentrant.activate(1);
    rejects([&] { reentrant.poll(1); });
    require(reentrant.state().faulted && reentrant.trace().empty(),
            "swallowed callback reentry faults before any following instruction");

    Audio bind_reentry_audio;
    odh::NativeHitController* bind_reentry_ptr = nullptr;
    auto bind_reentry_callbacks = bind_reentry_audio.callbacks();
    bind_reentry_callbacks.bind = [&](std::uint32_t) {
        bind_reentry_audio.events.push_back("bind-acquired");
        try { bind_reentry_ptr->pause(); } catch (const odh::FormatError&) {}
    };
    odh::NativeHitController bind_reentry(callback_code.hit,callback_code.hot,
        callback_code.track,16,args(),100,512,bind_reentry_callbacks);
    bind_reentry_ptr = &bind_reentry;
    rejects([&] { bind_reentry.activate(1); });
    require(bind_reentry.state().faulted && !bind_reentry.state().binding &&
            bind_reentry_audio.count("stop") == 1 &&
            bind_reentry_audio.count("release") == 1,
            "swallowed bind reentry tears down the successfully acquired current binding");
    bool externally_owned = false;
    auto strong_bind = bind_reentry_audio.callbacks();
    strong_bind.bind = [&](std::uint32_t) {
        externally_owned = true;
        externally_owned = false; // Adapter fulfills its exception guarantee.
        throw std::runtime_error("bind failed and self-cleaned");
    };
    odh::NativeHitController throwing_bind(callback_code.hit,callback_code.hot,
        callback_code.track,16,args(),100,512,strong_bind);
    bool bind_threw = false;
    try { throwing_bind.activate(1); } catch (const std::runtime_error&) { bind_threw = true; }
    require(bind_threw && throwing_bind.state().faulted && !externally_owned &&
            !throwing_bind.state().binding,
            "throwing bind's strong exception guarantee leaves no external binding");

    Fixture stop_callback_code{27};
    Audio stop_fault_audio;
    odh::NativeHitController* stop_fault_ptr = nullptr;
    auto stop_fault_callbacks = stop_fault_audio.callbacks();
    stop_fault_callbacks.stop_current = [&] {
        stop_fault_audio.events.push_back("stop-entered");
        try { stop_fault_ptr->pause(); } catch (const odh::FormatError&) {}
    };
    odh::NativeHitController stop_fault(stop_callback_code.hit,stop_callback_code.hot,
        stop_callback_code.track,16,args(),100,512,stop_fault_callbacks);
    stop_fault_ptr = &stop_fault;
    stop_fault.activate(1);
    rejects([&] { stop_fault.poll(1); });
    require(stop_fault.state().faulted && !stop_fault.state().binding &&
            stop_fault_audio.count("stop-entered") == 1 &&
            stop_fault_audio.count("release") == 1,
            "swallowed stop reentry still attempts acquired binding teardown");
    Audio throwing_stop_audio;
    auto throwing_stop_callbacks = throwing_stop_audio.callbacks();
    throwing_stop_callbacks.stop_current = [&] {
        throwing_stop_audio.events.push_back("stop-entered");
        throw std::runtime_error("backend stop failed");
    };
    odh::NativeHitController throwing_stop(stop_callback_code.hit,stop_callback_code.hot,
        stop_callback_code.track,16,args(),100,512,throwing_stop_callbacks);
    throwing_stop.activate(1);
    bool stop_threw = false;
    try { throwing_stop.poll(1); } catch (const std::runtime_error&) { stop_threw = true; }
    require(stop_threw && throwing_stop.state().faulted && !throwing_stop.state().binding &&
            throwing_stop_audio.count("release") == 1,
            "throwing stop keeps first error and releases acquired binding");
    Audio release_reentry_audio;
    odh::NativeHitController* release_reentry_ptr = nullptr;
    auto release_reentry_callbacks = release_reentry_audio.callbacks();
    release_reentry_callbacks.release_binding = [&] {
        release_reentry_audio.events.push_back("release-entered");
        try { release_reentry_ptr->pause(); } catch (const odh::FormatError&) {}
    };
    odh::NativeHitController release_reentry(stop_callback_code.hit,stop_callback_code.hot,
        stop_callback_code.track,16,args(),100,512,release_reentry_callbacks);
    release_reentry_ptr = &release_reentry;
    release_reentry.activate(1);
    rejects([&] { release_reentry.poll(1); });
    require(release_reentry.state().faulted && !release_reentry.state().binding &&
            release_reentry_audio.count("release-entered") == 1,
            "successful release records unbound state before swallowed reentry fault");
    Audio restart_throw_audio;
    auto restart_throw_callbacks = restart_throw_audio.callbacks();
    restart_throw_callbacks.stop_current = [&] {
        restart_throw_audio.events.push_back("restart-stop-entered");
        throw std::runtime_error("restart stop failed");
    };
    odh::NativeHitController restart_throw(restart.hit,restart.hot,restart.track,
        16,args(),100,512,restart_throw_callbacks);
    restart_throw.activate(1);
    restart_throw.poll(1);
    bool restart_threw = false;
    try { restart_throw.retrigger(args(9),100,512,4); }
    catch (const std::runtime_error&) { restart_threw = true; }
    require(restart_threw && restart_throw.state().faulted &&
            !restart_throw.state().binding && restart_throw_audio.count("release") == 1 &&
            restart_throw_audio.count("bind:100") == 1,
            "restart stop failure releases old binding without creating a new one");
    Audio restart_reentry_audio;
    odh::NativeHitController* restart_reentry_ptr = nullptr;
    auto restart_reentry_callbacks = restart_reentry_audio.callbacks();
    restart_reentry_callbacks.stop_current = [&] {
        restart_reentry_audio.events.push_back("restart-stop-entered");
        try { restart_reentry_ptr->pause(); } catch (const odh::FormatError&) {}
    };
    odh::NativeHitController restart_reentry(restart.hit,restart.hot,restart.track,
        16,args(),100,512,restart_reentry_callbacks);
    restart_reentry_ptr = &restart_reentry;
    restart_reentry.activate(1);
    restart_reentry.poll(1);
    rejects([&] { restart_reentry.retrigger(args(9),100,512,4); });
    require(restart_reentry.state().faulted && !restart_reentry.state().binding &&
            restart_reentry_audio.count("release") == 1 &&
            restart_reentry_audio.count("bind:100") == 1,
            "restart stop reentry releases old binding before faulting");

    Fixture ordered{27};
    odh::NativeHitController* observed = nullptr;
    std::vector<std::string> order;
    bool activity = false;
    odh::NativeHitAudio observing;
    observing.bind = [&](std::uint32_t) {
        require(!observed->state().active && observed->state().controls_17_47[46-17] == 0,
                "bind precedes initializer active and ID46 stores");
        order.push_back("bind");
    };
    observing.active = [&] { return activity; };
    observing.note_on = [&](std::uint8_t) {};
    observing.stop_current = [&] {
        require(!observed->state().active, "stop deactivates before audio stop callback");
        order.push_back("stop");
    };
    observing.release_binding = [&] { order.push_back("release"); };
    observing.pause_current = [&] {
        require(observed->state().active && observed->state().paused &&
                observed->state().controls_17_47[46-17] == 1,
                "pause callback observes active state and ID46");
        if (order.size() == 1)
            require(observed->state().positive,
                    "initializer pause observes positive flags from ID46 writer");
        order.push_back("pause");
    };
    observing.resume_current = [&] {
        require(!observed->state().paused, "resume callback observes cleared pause flag");
        order.push_back("resume");
    };
    observing.raw_control = [&](std::uint8_t id, std::int32_t value) {
        require(observed->state().controls_17_47[id-17] == value &&
                observed->state().zero == (value == 0) &&
                observed->state().positive == (value > 0) &&
                observed->state().sign == (value < 0),
                "raw callback observes completed native bank and flag store");
        order.push_back("raw:" + std::to_string(id));
    };
    odh::NativeHitController order_controller(ordered.hit,ordered.hot,ordered.track,
                                               16,args(),100,-5,observing);
    observed = &order_controller;
    order_controller.activate(1);
    require(order == std::vector<std::string>({"bind","pause","raw:19","raw:20","resume"}),
            "fresh activation orders binding, pause, caller spatial setters and resume");
    order_controller.pause(); order_controller.pause();
    require(order[order.size()-1] == "pause" && order[order.size()-2] == "pause",
            "native pause calls bound audio even when already paused");
    const auto old_order = order.size();
    order_controller.retrigger(args(4,5,6),111,-7,4);
    require(order.size() == old_order + 4 && order[old_order] == "resume" &&
            order[old_order+1] == "raw:19" && order[old_order+2] == "raw:20" &&
            order[old_order+3] == "resume" && order_controller.state().deadline == 4,
            "paused retrigger resumes, writes spatial controls, then resumes again");
    order_controller.stop();
    require(order[order.size()-2] == "stop" && order.back() == "release",
            "external stop requests audio stop then releases current binding");

    Fixture literal_flags{5,4,2,0,0,0,5,41,0,0,0,0,5,45,2,0,0,0,9,4};
    Audio lf;
    odh::NativeHitController raw_values(literal_flags.hit,literal_flags.hot,
        literal_flags.track,16,args(),100,512,lf.callbacks());
    raw_values.activate(1);
    require(raw_values.poll(1) == odh::NativeHitPoll::progressed &&
            raw_values.state().controls_17_47[41-17] == 0 &&
            raw_values.state().controls_17_47[45-17] == 2,
            "writer stores arbitrary raw ID41 and nonzero ID45 while active");
    rejects([&] { raw_values.poll(2); });
    require(raw_values.state().faulted,
            "unsupported fade destination rejects only when fade attempts it");

    std::vector<std::uint8_t> stop_at_102(101 * 6, 0);
    for (std::size_t n = 0; n < 101; ++n) {
        stop_at_102[n*6] = 5;
        stop_at_102[n*6+1] = 1;
    }
    stop_at_102.push_back(12);
    Fixture last_stop{};
    last_stop.hit.major_version = 1;
    last_stop.hit.bytes.resize(16);
    last_stop.hit.bytes.insert(last_stop.hit.bytes.end(),stop_at_102.begin(),stop_at_102.end());
    last_stop.hot.patches[100] = {"synthetic","synthetic.xa",false,false};
    last_stop.track = ordered.track;
    Audio ls;
    odh::NativeHitController diagnostic_after_stop(last_stop.hit,last_stop.hot,last_stop.track,
                                                    16,args(),100,512,ls.callbacks());
    diagnostic_after_stop.activate(1);
    require(diagnostic_after_stop.poll(1) == odh::NativeHitPoll::diagnostic_stopped &&
            diagnostic_after_stop.state().instructions == 102 &&
            diagnostic_after_stop.state().native_diagnostic_stop,
            "native post-102 diagnostic still records after opcode12 already stopped binding");

    Fixture profile{27};
    profile.track.hitlist_id.reset();
    Audio prof_audio;
    rejects([&] {
        odh::NativeHitController unresolved_entry(profile.hit,profile.hot,profile.track,
            16,args(),100,512,prof_audio.callbacks());
    });
    require(prof_audio.events.empty(), "unresolved HOT entry metadata fails before callbacks");

    Fixture keyed{2,7,9,1};
    keyed.track.hitlist_id = 777;
    // There is deliberately no HOT hitlist or TrackData row for key 777: the
    // caller has supplied the already resolved entry PC directly.
    Audio keyed_audio;
    odh::NativeHitController keyed_bound(keyed.hit,keyed.hot,keyed.track,
                                         16,args(),100,512,keyed_audio.callbacks());
    keyed_bound.activate(1);
    require(keyed_bound.poll(1) == odh::NativeHitPoll::progressed &&
            keyed_bound.state().controls_50_62[0] == 100 &&
            keyed_audio.count("bind:100") == 1 && keyed_audio.count("note:7") == 1,
            "nonzero HOT entry key executes supplied PC without chooser or replacing ID50");
    keyed.track.patch_id = 0;
    Audio keyed_zero_audio;
    odh::NativeHitController keyed_unbound(keyed.hit,keyed.hot,keyed.track,
                                           16,args(),100,512,keyed_zero_audio.callbacks());
    keyed_unbound.activate(1);
    require(keyed_unbound.poll(1) == odh::NativeHitPoll::progressed &&
            keyed_unbound.state().controls_50_62[0] == 0 &&
            keyed_zero_audio.count("bind:100") == 0 && keyed_zero_audio.count("note:7") == 0,
            "nonzero HOT entry key with patch zero creates no ghost binding or note");
    keyed.track.patch_id = 100;
    Audio prepared_stop_audio;
    odh::NativeHitController stopped_prepared(keyed.hit,keyed.hot,keyed.track,
                                               16,args(),100,512,prepared_stop_audio.callbacks());
    stopped_prepared.stop();
    require(stopped_prepared.state().controls_50_62[0] == 0 &&
            prepared_stop_audio.events.empty(),
            "stop before first activation clears ID50 without creating audio effects");
    stopped_prepared.activate(1);
    require(stopped_prepared.state().controls_50_62[0] == 100 &&
            stopped_prepared.poll(1) == odh::NativeHitPoll::progressed &&
            prepared_stop_audio.count("bind:100") == 1 &&
            prepared_stop_audio.count("note:7") == 1,
            "first activation after prepared stop rebuilds metadata bank and binds original patch");

    Audio rollback;
    auto broken = rollback.callbacks();
    broken.raw_control = [&](std::uint8_t, std::int32_t) {
        throw std::runtime_error("raw control failed");
    };
    odh::NativeHitController failed_activate(profile.hit,profile.hot,f.track,
                                             16,args(),100,512,broken);
    bool callback_failed = false;
    try { failed_activate.activate(1); }
    catch (const std::runtime_error&) { callback_failed = true; }
    require(callback_failed && failed_activate.state().faulted &&
            rollback.count("bind:100") == 1 && rollback.count("stop") == 1 &&
            rollback.count("release") == 1,
            "activation failure rolls back owned binding and latches execution fault");

    std::cout << "native HIT controller: " << checks << " checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "native HIT controller test failed: " << error.what() << '\n';
    return 1;
}
}
