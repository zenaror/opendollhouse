#include "odh/hit_native_controller.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
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
odh::Hot native_hot(const std::string& text) {
    std::istringstream stream(text);
    odh::Hsm symbols;
    return odh::read_hot(odh::View(stream, text.size()), &symbols, {},
                         odh::HotReadProfile::native_entry_map);
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

    // Three distinct HIT buffers deliberately have executable code at PC16.
    // Calls switch only bytecode identity: the originating HOT patch and banks
    // remain attached to the controller throughout the nested return chain.
    Fixture resource_a{7,1,0,0,0, 2,3, 12};
    Fixture resource_b{7,2,0,0,0, 2,2, 8};
    Fixture resource_c{2,1, 12};
    Audio cross_audio;
    odh::NativeHitResourceResolver cross_resolve = [&](std::uint32_t key)
            -> std::optional<odh::HitCodeTarget> {
        if (key == 1) return odh::HitCodeTarget{&resource_b.hit,16};
        if (key == 2) return odh::HitCodeTarget{&resource_c.hit,16};
        return std::nullopt;
    };
    odh::NativeHitController cross(resource_a.hit,resource_a.hot,resource_a.track,
        16,args(),100,512,cross_audio.callbacks(),{}, {},{},cross_resolve);
    require(&cross.current_hit() == &resource_a.hit,
            "prepared resource-aware controller starts at its entry HIT");
    cross.activate(1);
    require(cross.poll(1) == odh::NativeHitPoll::stopped &&
            cross.trace().size() == 8 && cross.state().call_depth == 0 &&
            &cross.current_hit() == &resource_a.hit,
            "nested cross-resource Call7/Return8/End12 restores the initial HIT");
    require(cross.trace()[0].source == &resource_a.hit &&
            cross.trace()[0].next_source == &resource_b.hit &&
            cross.trace()[0].pc == 16 && cross.trace()[0].next_pc == 16 &&
            cross.trace()[1].source == &resource_b.hit &&
            cross.trace()[1].next_source == &resource_c.hit &&
            cross.trace()[3].source == &resource_c.hit &&
            cross.trace()[3].next_source == &resource_b.hit &&
            cross.trace()[5].source == &resource_b.hit &&
            cross.trace()[5].next_source == &resource_a.hit,
            "trace distinguishes same PC across resources and each return transition");
    require(cross_audio.count("note:1") == 1 && cross_audio.count("note:2") == 1 &&
            cross_audio.count("note:3") == 1 && cross_audio.count("bind:100") == 1 &&
            cross.state().persistent[0] == 1,
            "cross-resource execution retains originating track binding and control bank");

    Fixture suspended_a{7,1,0,0,0}; // Saved return PC21 equals its code end.
    Fixture suspended_b{9,1, 8};
    Audio suspended_audio;
    odh::NativeHitController suspended(suspended_a.hit,suspended_a.hot,suspended_a.track,
        16,args(),100,512,suspended_audio.callbacks(),{}, {},{},
        [&](std::uint32_t) -> std::optional<odh::HitCodeTarget> {
            return odh::HitCodeTarget{&suspended_b.hit,16};
        });
    suspended.activate(1);
    const auto entry_flags = std::array<bool,3>{suspended.state().zero,
        suspended.state().positive,suspended.state().sign};
    require(suspended.poll(1) == odh::NativeHitPoll::progressed &&
            &suspended.current_hit() == &suspended_b.hit &&
            suspended.state().controls_50_62[0] == 100 &&
            std::array<bool,3>{suspended.state().zero,suspended.state().positive,
                               suspended.state().sign} == entry_flags,
            "cross-resource Call leaves patch bank and arithmetic flags unchanged");
    rejects([&] { suspended.poll(2); });
    require(suspended.state().faulted && suspended.trace().size() == 3 &&
            suspended.trace()[2].source == &suspended_b.hit &&
            suspended.trace()[2].next_source == &suspended_a.hit &&
            suspended.state().pc == 21 && suspended.state().call_depth == 0,
            "return to code end succeeds and only the subsequent fetch faults");

    Fixture inherited_a{7,1,0,0,0, 8};
    Fixture inherited_c{5,55,1,0,0,0, 9,1, 8};
    bool restart_route = false;
    Audio inherited_audio;
    odh::NativeHitController inherited(inherited_a.hit,inherited_a.hot,inherited_a.track,
        16,args(),100,512,inherited_audio.callbacks(),{}, {},{},
        [&](std::uint32_t key) -> std::optional<odh::HitCodeTarget> {
            if (key == 1) return odh::HitCodeTarget{&inherited_c.hit,restart_route ? 24U : 16U};
            if (key == 2) return odh::HitCodeTarget{&inherited_c.hit,16};
            return std::nullopt;
        });
    inherited.activate(1);
    // First route A -> C waits with one A frame. Restart retains that frame;
    // a second call into C returns through a newly pushed A frame, then the
    // inherited A frame. A separate A -> B -> C case below proves a B frame.
    require(inherited.poll(1) == odh::NativeHitPoll::progressed &&
            &inherited.current_hit() == &inherited_c.hit &&
            inherited.state().pc == 24 && inherited.state().call_depth == 1,
            "cross-resource wait leaves its active HIT and caller frame intact");
    inherited.retrigger(args(),100,512,2);
    require(&inherited.current_hit() == &inherited_a.hit &&
            inherited.state().pc == 16 && inherited.state().call_depth == 1,
            "ID55 restart restores entry HIT and PC while retaining frames");
    restart_route = true;
    require(inherited.poll(2) == odh::NativeHitPoll::stopped &&
            &inherited.current_hit() == &inherited_a.hit &&
            inherited.state().call_depth == 0,
            "post-restart returns consume both new and inherited resource frames");

    Fixture frame_a{7,1,0,0,0, 8};
    Fixture frame_b{7,2,0,0,0, 8};
    Fixture frame_c{5,55,1,0,0,0, 9,1, 8};
    bool route_after_restart = false;
    Audio frame_audio;
    odh::NativeHitController frame_chain(frame_a.hit,frame_a.hot,frame_a.track,
        16,args(),100,512,frame_audio.callbacks(),{}, {},{},
        [&](std::uint32_t key) -> std::optional<odh::HitCodeTarget> {
            if (key == 1) return route_after_restart ? odh::HitCodeTarget{&frame_c.hit,24}
                                                     : odh::HitCodeTarget{&frame_b.hit,16};
            if (key == 2) return odh::HitCodeTarget{&frame_c.hit,16};
            return std::nullopt;
        });
    frame_chain.activate(1);
    require(frame_chain.poll(1) == odh::NativeHitPoll::progressed &&
            frame_chain.state().call_depth == 2 &&
            &frame_chain.current_hit() == &frame_c.hit,
            "two resource identities remain in nested pending frames");
    frame_chain.retrigger(args(),100,512,2);
    route_after_restart = true;
    require(frame_chain.poll(2) == odh::NativeHitPoll::stopped &&
            frame_chain.trace()[6].source == &frame_a.hit &&
            frame_chain.trace()[6].next_source == &frame_b.hit &&
            frame_chain.trace()[7].source == &frame_b.hit &&
            frame_chain.trace()[7].next_source == &frame_a.hit &&
            frame_chain.state().call_depth == 0,
            "inherited frame restores the intermediate HIT after restart");
    frame_chain.stop();
    require(&frame_chain.current_hit() == &frame_a.hit &&
            frame_chain.state().pc == 16 && frame_chain.state().call_depth == 0,
            "explicit stop resets resource, PC and saved frames");

    Fixture target_source{7,9,0,0,0};
    Fixture target_valid{8};
    Fixture target_bad_version{8}; target_bad_version.hit.major_version = 2;
    Fixture target_empty{};
    Fixture target_entp{8,8}; target_entp.hit.entry_table_offset = 17;
    Fixture target_bad_region{8}; target_bad_region.hit.entry_table_offset = 30;
    std::vector<odh::HitCodeTarget> bad_targets = {
        {nullptr,16}, {&target_bad_version.hit,16}, {&target_empty.hit,16},
        {&target_entp.hit,17}, {&target_bad_region.hit,16},
        {&target_valid.hit,0}, {&target_valid.hit,17}
    };
    for (const auto bad_target : bad_targets) {
        Audio target_audio;
        odh::NativeHitController invalid_resource(target_source.hit,target_source.hot,
            target_source.track,16,args(),100,512,target_audio.callbacks(),{}, {},{},
            [bad_target](std::uint32_t) -> std::optional<odh::HitCodeTarget> {
                return bad_target;
            });
        invalid_resource.activate(1);
        rejects([&] { invalid_resource.poll(1); });
        require(invalid_resource.state().faulted && invalid_resource.trace().empty() &&
                invalid_resource.state().call_depth == 0 &&
                &invalid_resource.current_hit() == &target_source.hit,
                "invalid resource target faults before switching HIT or saving return");
    }
    Audio no_target_audio;
    odh::NativeHitController no_resource(target_source.hit,target_source.hot,
        target_source.track,16,args(),100,512,no_target_audio.callbacks(),{}, {},{},
        [](std::uint32_t) -> std::optional<odh::HitCodeTarget> { return std::nullopt; });
    no_resource.activate(1);
    rejects([&] { no_resource.poll(1); });
    require(no_resource.state().call_depth == 0 && no_resource.trace().empty(),
            "missing resource key faults before pushing a return");
    rejects([&] {
        odh::NativeHitController ambiguous(target_source.hit,target_source.hot,
            target_source.track,16,args(),100,512,no_target_audio.callbacks(),{}, {},
            [](std::uint32_t) -> std::optional<std::uint32_t> { return 16; },
            [](std::uint32_t) -> std::optional<odh::HitCodeTarget> { return std::nullopt; });
    });
    Audio resource_reentry_audio;
    odh::NativeHitController* resource_reentry_ptr = nullptr;
    odh::NativeHitController resource_reentry(target_source.hit,target_source.hot,
        target_source.track,16,args(),100,512,resource_reentry_audio.callbacks(),{}, {},{},
        [&](std::uint32_t) -> std::optional<odh::HitCodeTarget> {
            try { resource_reentry_ptr->pause(); } catch (const odh::FormatError&) {}
            return odh::HitCodeTarget{&target_valid.hit,16};
        });
    resource_reentry_ptr = &resource_reentry;
    resource_reentry.activate(1);
    rejects([&] { resource_reentry.poll(1); });
    require(resource_reentry.state().faulted && resource_reentry.trace().empty() &&
            resource_reentry.state().call_depth == 0 &&
            &resource_reentry.current_hit() == &target_source.hit,
            "swallowed resource resolver reentry faults before resource switch");

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

    // The selected event, winning Track, entry HIT and winning Patch each
    // originate in a different group. The local HOT carries a shadow Patch
    // that must never supply the global binding.
    Fixture global_code{2,7,5,55,1,0,0,0,9,1};
    auto event_hot = native_hot("[EventMapping]\nglobal=1,7\n");
    auto track_hot = native_hot(
        "[Track]\n7=0,global,1024,2,0,0,20,300,80\n");
    auto entry_hot = native_hot("[TrackData]\n300=16\n");
    auto patch_hot = native_hot(
        "[Patch]\n80=0,remote,\"remote.xa\",0,0\n");
    event_hot.patches[80] = {"local-shadow", "local.xa", false, false};
    event_hot.patches[81] = {"missing-shadow", "missing.xa", false, false};
    const std::vector<odh::HitEntryGroup> global_groups{
        {&global_code.hit, &event_hot}, {&global_code.hit, &track_hot},
        {&global_code.hit, &entry_hot}, {&global_code.hit, &patch_hot}};
    const odh::HitEntryCatalog global_entries(global_groups);
    const odh::NativeHitObjectCatalog global_objects(global_groups, global_entries);
    const auto global_event = global_objects.resolve_global_track_event(0, "global");
    require(global_event.event_group == 0 && global_event.track.object.group == 1 &&
            global_event.track.entry.file == &global_code.hit &&
            global_event.track.entry.pc == 16 &&
            global_objects.lookup_patch(80)->group == 3,
            "global event, Track, entry and Patch retain separate origins");
    const odh::NativeHitPatchSource global_source{
        odh::NativeHitPatchSource::Scope::global_catalog, &global_objects};
    Audio global_audio;
    auto global_callbacks = global_audio.callbacks();
    global_callbacks.bind = {}; // Global profile has no numeric-ID fallback.
    std::vector<std::string> global_phases;
    global_callbacks.bind_global_patch =
        [&](const odh::NativeHitPatchRef& ref, const odh::NativeHitState& snapshot) {
            require(ref.id == 80 && ref.group == 3 &&
                    ref.patch == &patch_hot.patches.at(80) &&
                    snapshot.controls_17_47[26 - 17] != 77,
                    "first global phase sees winning Patch and pre-getter ID26");
            global_phases.push_back("first");
            return 77;
        };
    global_callbacks.apply_global_controls = [&](const odh::NativeHitState& snapshot) {
        require(snapshot.controls_17_47[26 - 17] == 77 && snapshot.positive &&
                snapshot.binding,
                "second global phase sees getter-derived ID26 and acquired binding");
        global_phases.push_back("second");
    };
    rejects([&] {
        odh::NativeHitController no_catalog(global_code.hit, event_hot,
            *global_event.track.object.track, 16, args(), 100, 512,
            global_callbacks, {}, {}, {}, {},
            {odh::NativeHitPatchSource::Scope::global_catalog, nullptr});
    });
    require(global_audio.events.empty() && global_phases.empty(),
            "invalid global source rejects before any audio callback");
    odh::NativeHitController global_controller(*global_event.track.entry.file,
        event_hot, *global_event.track.object.track, global_event.track.entry.pc,
        args(), 100, 512, global_callbacks, {}, {}, {}, {}, global_source);
    require(global_phases.empty() && global_audio.events.empty() &&
            global_controller.state().last_binding_outcome ==
                odh::NativeHitBindingOutcome::not_attempted,
            "global constructor is a pure staged snapshot");
    global_controller.activate(1);
    require(global_phases == std::vector<std::string>({"first", "second"}) &&
            global_controller.state().binding &&
            global_controller.state().controls_17_47[26 - 17] == 77 &&
            global_controller.state().last_binding_outcome ==
                odh::NativeHitBindingOutcome::acquired &&
            global_audio.count("bind:80") == 0,
            "global binding uses winning Patch callback and two control phases");
    require(global_controller.poll(1) == odh::NativeHitPoll::progressed &&
            global_audio.count("note:7") == 1,
            "global acquired binding executes NoteOn through current audio view");
    global_controller.retrigger(args(9), 100, 512, 4);
    require(global_phases.size() == 4 && global_audio.count("release") == 1 &&
            global_controller.state().last_binding_outcome ==
                odh::NativeHitBindingOutcome::acquired,
            "ID55 restart reacquires the same global Patch after releasing old binding");

    auto zero_track = *global_event.track.object.track;
    zero_track.patch_id = 0;
    Audio zero_global_audio;
    auto zero_global_callbacks = zero_global_audio.callbacks();
    zero_global_callbacks.bind_global_patch = global_callbacks.bind_global_patch;
    zero_global_callbacks.apply_global_controls = global_callbacks.apply_global_controls;
    odh::NativeHitController zero_global(global_code.hit, event_hot, zero_track, 16,
        args(), 100, 512, zero_global_callbacks, {}, {}, {}, {}, global_source);
    zero_global.activate(1);
    require(zero_global.state().active && !zero_global.state().binding &&
            zero_global.state().last_binding_outcome == odh::NativeHitBindingOutcome::zero &&
            global_phases.size() == 4,
            "global Patch ID zero has no lookup or backend phases");

    auto missing_track = *global_event.track.object.track;
    missing_track.patch_id = 81;
    Audio missing_audio;
    auto missing_callbacks = missing_audio.callbacks();
    missing_callbacks.bind_global_patch = global_callbacks.bind_global_patch;
    missing_callbacks.apply_global_controls = global_callbacks.apply_global_controls;
    odh::NativeHitController missing(global_code.hit, event_hot, missing_track, 16,
        args(), 100, 512, missing_callbacks, {}, {}, {}, {}, global_source);
    missing.activate(3);
    require(missing.state().active && !missing.state().binding &&
            missing.state().controls_50_62[0] == 81 && missing.state().pc == 16 &&
            missing.state().deadline == 3 &&
            missing.state().last_binding_outcome == odh::NativeHitBindingOutcome::missing &&
            missing_audio.events.empty(),
            "missing global Patch keeps requested ID50 and active initializer state");
    require(missing.poll(3) == odh::NativeHitPoll::progressed &&
            missing_audio.count("note:7") == 0,
            "missing global Patch executes bytecode without a wrapper");

    auto track_shadow = native_hot(
        "[Track]\n80=0,mask,1024,2,0,0,20,300,0\n");
    const std::vector<odh::HitEntryGroup> shadow_groups{
        {&global_code.hit, &event_hot}, {&global_code.hit, &track_hot},
        {&global_code.hit, &entry_hot}, {&global_code.hit, &patch_hot},
        {&global_code.hit, &track_shadow}};
    const odh::HitEntryCatalog shadow_entries(shadow_groups);
    const odh::NativeHitObjectCatalog shadow_objects(shadow_groups, shadow_entries);
    const odh::NativeHitPatchSource shadow_source{
        odh::NativeHitPatchSource::Scope::global_catalog, &shadow_objects};
    Audio wrong_audio;
    auto wrong_callbacks = wrong_audio.callbacks();
    wrong_callbacks.bind_global_patch = global_callbacks.bind_global_patch;
    wrong_callbacks.apply_global_controls = global_callbacks.apply_global_controls;
    odh::NativeHitController wrong(global_code.hit, event_hot,
        *global_event.track.object.track, 16, args(), 100, 512, wrong_callbacks,
        {}, {}, {}, {}, shadow_source);
    wrong.activate(5);
    require(wrong.state().active && !wrong.state().binding &&
            wrong.state().controls_50_62[0] == 0 && wrong.state().pc == 16 &&
            wrong.state().deadline == 5 && wrong.state().call_depth == 0 &&
            wrong.state().controls_17_47[46 - 17] == 1 &&
            wrong.state().last_binding_outcome == odh::NativeHitBindingOutcome::wrong_kind &&
            wrong_audio.events.empty() && global_phases.size() == 4,
            "winning Track triggers internal stop then initializer continues without fallback");
    Fixture wrong_stack_code{7,1,0,0,0,9,1,5,55,1,0,0,0,9,1};
    Audio wrong_stack_audio;
    auto wrong_stack_callbacks = wrong_stack_audio.callbacks();
    wrong_stack_callbacks.bind_global_patch = global_callbacks.bind_global_patch;
    wrong_stack_callbacks.apply_global_controls = global_callbacks.apply_global_controls;
    odh::NativeHitController wrong_stack(wrong_stack_code.hit, event_hot,
        *global_event.track.object.track, 16, args(), 100, 512,
        wrong_stack_callbacks, {}, {},
        [](std::uint32_t key) -> std::optional<std::uint32_t> {
            if (key == 1) return 23;
            return std::nullopt;
        }, {}, shadow_source);
    wrong_stack.activate(1);
    require(wrong_stack.poll(1) == odh::NativeHitPoll::progressed &&
            wrong_stack.state().call_depth == 1 &&
            wrong_stack.state().controls_50_62[55 - 50] == 1,
            "wrong-kind controller can retain a Call7 frame until restart");
    wrong_stack.retrigger(args(9), 100, 512, 4);
    require(wrong_stack.state().active && wrong_stack.state().call_depth == 0 &&
            wrong_stack.state().pc == 16 && wrong_stack.state().deadline == 4 &&
            wrong_stack.state().last_binding_outcome ==
                odh::NativeHitBindingOutcome::wrong_kind,
            "wrong-kind internal stop clears inherited Call7 frames before continuing");

    Audio global_reentry_audio;
    auto global_reentry_callbacks = global_reentry_audio.callbacks();
    odh::NativeHitController* global_reentry_ptr = nullptr;
    global_reentry_callbacks.bind_global_patch =
        [&](const odh::NativeHitPatchRef&, const odh::NativeHitState&) {
            try { global_reentry_ptr->pause(); } catch (const odh::FormatError&) {}
            return 1;
        };
    global_reentry_callbacks.apply_global_controls = [](const odh::NativeHitState&) {};
    odh::NativeHitController global_reentry(global_code.hit, event_hot,
        *global_event.track.object.track, 16, args(), 100, 512,
        global_reentry_callbacks, {}, {}, {}, {}, global_source);
    global_reentry_ptr = &global_reentry;
    rejects([&] { global_reentry.activate(1); });
    require(global_reentry.state().faulted && !global_reentry.state().binding &&
            global_reentry_audio.count("stop") == 1 &&
            global_reentry_audio.count("release") == 1,
            "swallowed global bind reentry tears down acquired Patch");
    Audio global_first_fail_audio;
    auto global_first_fail_callbacks = global_first_fail_audio.callbacks();
    bool external_first_binding = false;
    global_first_fail_callbacks.bind_global_patch =
        [&](const odh::NativeHitPatchRef&, const odh::NativeHitState&) -> std::int32_t {
            external_first_binding = true;
            external_first_binding = false; // Callback keeps its strong guarantee.
            throw std::runtime_error("first phase failed");
        };
    global_first_fail_callbacks.apply_global_controls = [](const odh::NativeHitState&) {};
    odh::NativeHitController global_first_fail(global_code.hit, event_hot,
        *global_event.track.object.track, 16, args(), 100, 512,
        global_first_fail_callbacks, {}, {}, {}, {}, global_source);
    bool first_failed = false;
    try { global_first_fail.activate(1); }
    catch (const std::runtime_error&) { first_failed = true; }
    require(first_failed && global_first_fail.state().faulted &&
            !global_first_fail.state().binding && !external_first_binding &&
            global_first_fail_audio.count("stop") == 0 &&
            global_first_fail_audio.count("release") == 0,
            "throwing first global phase leaves no acquired binding");
    Audio global_second_fail_audio;
    auto global_second_fail_callbacks = global_second_fail_audio.callbacks();
    global_second_fail_callbacks.bind_global_patch =
        [](const odh::NativeHitPatchRef&, const odh::NativeHitState&) { return 8; };
    global_second_fail_callbacks.apply_global_controls =
        [](const odh::NativeHitState&) { throw std::runtime_error("second phase failed"); };
    odh::NativeHitController global_second_fail(global_code.hit, event_hot,
        *global_event.track.object.track, 16, args(), 100, 512,
        global_second_fail_callbacks, {}, {}, {}, {}, global_source);
    bool second_failed = false;
    try { global_second_fail.activate(1); }
    catch (const std::runtime_error&) { second_failed = true; }
    require(second_failed && global_second_fail.state().faulted &&
            !global_second_fail.state().binding &&
            global_second_fail_audio.count("stop") == 1 &&
            global_second_fail_audio.count("release") == 1,
            "second global control phase failure tears down acquired binding");

    std::cout << "native HIT controller: " << checks << " checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "native HIT controller test failed: " << error.what() << '\n';
    return 1;
}
}
