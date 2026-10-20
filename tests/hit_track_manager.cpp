#include "odh/hit_track_manager.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
unsigned checks = 0, cases = 0, failed = 0;
void check(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
template<class F> void rejects(F action, const char* reason) {
    bool rejected = false;
    try { action(); } catch (const odh::FormatError& error) {
        rejected = std::string(error.what()).find(reason) != std::string::npos;
    }
    check(rejected, "expected specific FormatError");
}
template<class F> void run(const char* name, F action) {
    ++cases;
    try { action(); std::cout << "CASE " << name << " PASS\n"; }
    catch (const std::exception& error) {
        ++failed; std::cout << "CASE " << name << " FAIL " << error.what() << '\n';
    }
}
odh::HitFile code() {
    odh::HitFile hit;
    hit.major_version = 1; hit.minor_version = 8; hit.bytes.resize(16);
    const std::uint8_t bytes[]{5,4,10,0,0,0,9,4,27};
    hit.bytes.insert(hit.bytes.end(), std::begin(bytes), std::end(bytes));
    return hit;
}
odh::Hot hot(const std::string& text) {
    std::istringstream input(text);
    odh::Hsm hsm;
    return odh::read_hot(odh::View(input, text.size()), &hsm, {},
                         odh::HotReadProfile::native_entry_map);
}
const char* base =
    "[TrackData]\n100=16\n200=16\n"
    "[Track]\n7=0,one,1024,2,0,0,20,100,1000\n"
    "8=0,two,1024,2,0,0,20,200,1000\n"
    "[Patch]\n1000=0,patch,\"patch.xa\",0,0\n"
    "[EventMapping]\none=1,7\nalias=1,7\ntwo=1,8\n"
    "override=1,7,3,44,55\nmissing=1,99\nwrong=1,1000\n";
struct Fixture {
    odh::HitFile a = code(), b = code(), c = code();
    odh::Hot metadata = hot(base);
    odh::Hot replacement = hot("[Track]\n7=0,new,777,2,0,0,20,300,1000\n");
    odh::Hot entries = hot("[TrackData]\n300=16\n");
    std::vector<odh::HitEntryGroup> groups;
    odh::HitEntryCatalog entry;
    odh::NativeHitObjectCatalog object;
    std::unique_ptr<odh::HitTrackManager> manager;
    std::map<std::int32_t, odh::HitSourceControls> samples{
        {1,{100,0}}, {2,{300,1024}}, {3,{200,512}}};
    std::map<std::uint32_t, bool> playing{{7,true},{8,true}};
    std::vector<std::string> events;
    bool admit = true, group = true;
    unsigned factories = 0;
    std::function<void()> admission_hook, group_hook, source_hook, factory_hook, audio_hook;
    explicit Fixture(bool conflict = false, odh::HitTrackManagerLimits limits = {4,8})
        : groups(conflict ? std::vector<odh::HitEntryGroup>{{&a,&metadata},{&b,&replacement},{&c,&entries}}
                          : std::vector<odh::HitEntryGroup>{{&a,&metadata}}),
          entry(groups), object(groups, entry) {
        odh::HitTrackManagerProviders providers;
        providers.admission = [&](std::uint32_t id, const odh::NativeHitState&) {
            events.push_back("admit:"+std::to_string(id));
            if (admission_hook) admission_hook();
            return admit;
        };
        providers.initializer_group = [&](std::uint32_t id, const odh::NativeHitState&) {
            events.push_back("group:"+std::to_string(id));
            if (group_hook) group_hook();
            return group;
        };
        providers.source = [&](std::uint32_t id, std::int32_t source, const odh::NativeHitState&)
            -> std::optional<odh::HitSourceControls> {
            events.push_back("source:"+std::to_string(id)+":"+std::to_string(source));
            if (source_hook) source_hook();
            const auto found = samples.find(source);
            return found == samples.end() ? std::nullopt : std::optional<odh::HitSourceControls>(found->second);
        };
        manager = std::make_unique<odh::HitTrackManager>(object, std::move(providers), limits);
    }
    void register_track(std::uint32_t id = 7) {
        manager->register_track(id, [&](const odh::ResolvedGlobalHitTrack& resolved) {
            ++factories;
            if (factory_hook) factory_hook();
            const auto track = resolved.object.id;
            odh::NativeHitAudio audio;
            audio.active = [&,track] { events.push_back("active:"+std::to_string(track)); return playing.at(track); };
            audio.stop_current = [&,track] { events.push_back("stop:"+std::to_string(track)); };
            audio.release_binding = [&,track] { events.push_back("release:"+std::to_string(track)); };
            audio.pause_current = [&,track] { events.push_back("pause:"+std::to_string(track)); };
            audio.resume_current = [&,track] { events.push_back("resume:"+std::to_string(track)); };
            audio.bind_global_patch = [&,track](const odh::NativeHitPatchRef&, const odh::NativeHitState&) {
                events.push_back("bind:"+std::to_string(track)); return 0;
            };
            audio.apply_global_controls = [](const odh::NativeHitState&) {};
            audio.apply_global_control_write = [&,track](std::uint8_t id, const odh::NativeHitState& state) {
                events.push_back("control:"+std::to_string(track)+":"+std::to_string(id)+":"+
                    std::to_string(state.controls_17_47[id-17]));
                if (audio_hook) audio_hook();
            };
            const auto target = resolved.entry;
            odh::NativeHitLimits budget{256,256,256,4};
            return std::make_unique<odh::NativeHitController>(*target.file,
                *groups[resolved.object.group].hot, *resolved.object.track, target.pc,
                odh::HitArguments{{9,8,7}}, 0, 512, std::move(audio), budget,
                odh::HitNativeControllerValues{}, odh::NativeHitCallResolver{},
                [&](std::uint32_t key) { return entry.lookup(key); },
                odh::NativeHitPatchSource{odh::NativeHitPatchSource::Scope::global_catalog,&object});
        });
    }
    odh::HitTrackPlayResult play(const char* name = "one", std::int32_t source = 1,
                                std::uint32_t clock = 1) {
        return manager->play(0,name,{{source,20,30}},clock);
    }
    const odh::NativeHitState& state(std::uint32_t id = 7) const {
        return manager->controller(id)->state();
    }
    unsigned count(const std::string& event) const {
        return static_cast<unsigned>(std::count(events.begin(),events.end(),event));
    }
};
}
int main() {
    run("global-winning-track-and-entry", [] {
        Fixture f(true); f.register_track(); f.play();
        check(&f.manager->controller(7)->current_hit() == &f.c, "global entry resource C wins");
        check(f.state().controls_17_47[18-17] == 777, "global Track B metadata wins");
    });
    run("alias-reuses-controller-and-pair", [] {
        Fixture f; f.register_track(); const auto* before = f.manager->controller(7);
        check(f.play().contribution_inserted, "first pair inserted");
        check(!f.play("alias",1,2).contribution_inserted, "alias pair idempotent");
        check(f.factories == 1 && f.manager->registered_count() == 1 &&
            f.manager->controller(7) == before && f.manager->source_count(7) == 1,
            "same persistent controller for alias and retrigger");
        check(f.count("bind:7") == 1 && f.state().deadline == 2, "active retrigger preserves binding");
    });
    run("missing-event", [] { Fixture f; f.register_track(); rejects([&]{f.play("absent");},"event missing"); });
    run("missing-global-track", [] { Fixture f; f.register_track(); rejects([&]{f.play("missing");},"TrackID absent"); });
    run("wrong-kind-global-track", [] { Fixture f; f.register_track(); rejects([&]{f.play("wrong");},"different object kind"); });
    run("unregistered-track-no-lazy-factory", [] {
        Fixture f; rejects([&]{f.play();},"not registered");
        check(f.factories == 0 && !f.manager->registered_count(), "Play cannot create controller");
    });
    run("effective-argument-overrides", [] {
        Fixture f; f.register_track(); const auto result = f.play("override",1);
        check(result.arguments == odh::HitArguments{{3,44,55}} && f.manager->contains_source(7,3) &&
            !f.manager->contains_source(7,1), "effective R1 source and independent R2/R3 overrides");
        check(f.state().persistent[0] == 3 && f.state().persistent[1] == 44 &&
            f.state().persistent[2] == 55 && f.state().controls_17_47[19-17] == 200,
            "raw args distinct from aggregate spatial controls");
    });
    run("zero-r1-after-admission", [] {
        Fixture f; f.register_track(); const auto result = f.play("one",0);
        check(result.status == odh::HitTrackPlayStatus::zero_source && f.count("admit:7") == 1 &&
            !f.count("group:7") && !f.manager->source_count(7), "zero refuses after admission before insertion");
    });
    run("active-admission-refusal", [] {
        Fixture f; f.register_track(); f.play(); const auto before = f.state();
        f.admit = false; const auto result = f.play("alias",2,5);
        check(result.status == odh::HitTrackPlayStatus::admission_refused &&
            f.count("admit:7") == 2 && f.manager->source_count(7) == 1 &&
            f.state().persistent == before.persistent && f.state().deadline == before.deadline,
            "active still gated without new pair or controller mutation");
    });
    run("inactive-clear-before-admission", [] {
        Fixture f; f.register_track(); f.play(); f.play("one",2); f.manager->stop_track(7);
        check(f.manager->source_count(7) == 2, "stop retains contributions until next Play");
        f.admit = false; f.admission_hook = [&] { check(!f.manager->source_count(7), "clear visible at admission"); };
        check(f.play().status == odh::HitTrackPlayStatus::admission_refused &&
            !f.manager->source_count(7) && f.manager->registered_count() == 1,
            "refusal retains cleared table and persistent controller");
    });
    run("multi-source-weighted-aggregate", [] {
        Fixture f; f.register_track(); f.play(); f.manager->poll_track(7,1);
        const auto pc = f.state().pc; const auto source47 = f.state().controls_17_47[47-17];
        f.play("one",2,2);
        check(f.manager->source_count(7) == 2 && f.state().controls_17_47[19-17] == 300 &&
            f.state().controls_17_47[20-17] == 768, "max volume and weighted pan");
        check(f.state().persistent[0] == 2 && f.state().controls_17_47[47-17] == source47 &&
            f.state().pc == pc && f.state().persistent[3] == 10, "active retrigger preserves PC/source47/R4");
    });
    run("special-negative-source", [] {
        Fixture f; f.register_track(); f.play("one",-1);
        check(f.state().controls_17_47[19-17] == 1024 && f.state().controls_17_47[20-17] == 512 &&
            !f.count("source:7:-1"), "nonpositive sample is explicit unity/center without World lookup");
    });
    run("positive-unavailable-refuses-before-insert", [] {
        Fixture f; f.register_track(); f.play(); const auto before = f.state();
        check(f.play("one",99).status == odh::HitTrackPlayStatus::source_unavailable &&
            !f.manager->contains_source(7,99) && f.manager->source_count(7) == 1 &&
            f.state().persistent == before.persistent && !f.manager->faulted(), "ordinary missing source refuses cleanly");
    });
    run("pair-capacity-and-duplicate", [] {
        Fixture f(false,{2,1}); f.register_track(); f.play();
        check(f.play("alias").status == odh::HitTrackPlayStatus::dispatched, "duplicate accepted at capacity");
        check(f.play("one",2).status == odh::HitTrackPlayStatus::capacity_refused &&
            f.manager->source_count(7) == 1 && !f.manager->faulted(), "new pair refused at bound");
    });
    run("group-gate-inactive-still-caller-setters", [] {
        Fixture f; f.register_track(); f.group = false; const auto result = f.play();
        check(!result.initializer_enabled && f.manager->source_count(7) == 1 && !f.state().active &&
            f.state().persistent[0] == 9 && f.state().persistent[1] == 8 &&
            f.state().persistent[2] == 7 && !f.count("bind:7") &&
            f.state().controls_17_47[19-17] == 100 && f.state().controls_17_47[20-17] == 0,
            "skipped initializer preserves prepared arguments but caller writes spatial controls");
        f.group = true; f.play("one",2,3);
        check(f.state().active && f.state().persistent[0] == 2 &&
            f.manager->source_count(7) == 1, "later allowed inactive request clears old pair and initializes");
    });
    run("group-gate-active-preserves-args-but-updates-controls-deadline", [] {
        Fixture f; f.register_track(); f.play(); f.manager->poll_track(7,1);
        const auto before = f.state(); f.group = false; f.play("one",2,5);
        check(f.state().persistent == before.persistent && f.state().pc == before.pc &&
            f.state().controls_17_47[19-17] == 300 && f.state().controls_17_47[20-17] == 768 &&
            f.state().deadline == 5 && f.manager->source_count(7) == 2,
            "early initializer return leaves caller effects intact");
    });
    run("remove-source-interleaves-track-effects", [] {
        Fixture f; f.register_track(); f.register_track(8);
        f.play(); f.play("one",2); f.play("two",1); f.play("two",3);
        f.events.clear();
        f.audio_hook = [&] {
            if (f.events.back().find("control:7:") == 0)
                check(f.manager->contains_source(8,1), "track8 pair survives during track7 effect");
        };
        check(f.manager->remove_source(1,false) == 2, "both source pairs removed");
        check(f.manager->source_count(7) == 1 && f.manager->source_count(8) == 1 &&
            f.state(7).controls_17_47[19-17] == 300 && f.state(8).controls_17_47[19-17] == 200,
            "remaining contribution applied separately per track");
    });
    run("last-source-fades-and-retains-controller", [] {
        Fixture f; f.register_track(); f.play(); const auto* before = f.manager->controller(7);
        check(f.manager->remove_source(1,false) == 1 && f.state().fade_stop_pending &&
            f.state().controls_17_47[40-17] == 0 && f.state().controls_17_47[41-17] == 19 &&
            f.state().controls_17_47[42-17] == 100 && f.state().controls_17_47[45-17] == 1,
            "last contribution requests native fade controls");
        check(f.manager->controller(7) == before && f.manager->registered_count() == 1 &&
            !f.manager->source_count(7), "fade does not erase controller");
    });
    run("last-source-inactive-audio-stops-immediately", [] {
        Fixture f; f.register_track(); f.play(); f.playing[7] = false;
        f.manager->remove_source(1,false);
        check(!f.state().active && f.count("stop:7") == 1 && f.count("release:7") == 1 &&
            f.manager->controller(7), "unplaying audio immediate stop retains controller");
    });
    run("remove-exact-negative-and-absent-source", [] {
        Fixture f; f.register_track(); f.play("one",-1); f.play("one",-2);
        check(f.manager->remove_source(-1,false) == 1 && f.manager->contains_source(7,-2) &&
            f.manager->remove_source(0,false) == 0 && f.manager->remove_source(99,false) == 0,
            "negative is exact source, not wildcard");
    });
    for (const auto volume : {0,1}) run(("play-raw-vs-refresh-adjustment-volume-"+std::to_string(volume)).c_str(), [volume] {
        Fixture f; f.register_track(); f.samples[1] = {volume,512};
        f.play();
        check(f.state().controls_17_47[19-17] == volume,
            "initial type1 dispatch uses raw aggregate without service decrement");
        f.manager->refresh_track(7,true);
        check(f.state().controls_17_47[19-17] == (volume == 0 ? 1 : 0),
            "explicit refresh condition applies zero->1 and one->0");
        f.play("one",1,2);
        check(f.state().controls_17_47[19-17] == volume,
            "active type1 dispatch restores raw aggregate without refresh decrement");
        f.manager->refresh_track(7,false);
        check(f.state().controls_17_47[19-17] == volume,
            "refresh without explicit condition keeps raw aggregate");
    });
    for (const auto failure : {0,1,2,3}) run(("pre-effect-refusal-recovers-kind-"+std::to_string(failure)).c_str(), [failure] {
        Fixture f; f.register_track(); f.play();
        const auto before = f.state(); const auto event_count = f.events.size();
        const char* name = failure == 0 ? "absent" : failure == 1 ? "two" : failure == 2 ? "wrong" : "missing";
        const char* reason = failure == 0 ? "event missing" : failure == 1 ? "not registered" : failure == 2 ? "different object kind" : "TrackID absent";
        rejects([&]{f.play(name,2,2);},reason);
        check(!f.manager->faulted() && f.events.size() == event_count &&
            f.manager->source_count(7) == 1 && f.state().persistent == before.persistent &&
            f.state().pc == before.pc && f.state().deadline == before.deadline,
            "lookup refusal has no runtime/provider effect and no fault latch");
        check(f.play("alias",2,3).status == odh::HitTrackPlayStatus::dispatched &&
            f.manager->poll_track(7,3) == odh::NativeHitPoll::progressed,
            "next valid dispatch and poll remain usable");
        f.manager->stop_track(7);
        check(!f.state().active && !f.manager->faulted(), "stop still works after refusal recovery");
    });
    run("controller-budget", [] {
        Fixture f(false,{1,8}); f.register_track(); f.play();
        rejects([&]{f.register_track(8);},"controller budget");
        check(f.factories == 1 && f.manager->registered_count() == 1 && !f.manager->faulted(),
            "budget enforced before factory without faulting");
        check(f.manager->poll_track(7,1) == odh::NativeHitPoll::progressed &&
            f.play("alias",2,2).status == odh::HitTrackPlayStatus::dispatched,
            "existing controller remains poll/play-capable after registration budget refusal");
        f.manager->stop_track(7);
        check(!f.state().active && !f.manager->faulted(), "stop after registration budget refusal");
    });
    run("duplicate-registration", [] {
        Fixture f; f.register_track(); f.play();
        rejects([&]{f.register_track();},"already registered");
        check(f.factories == 1 && !f.manager->faulted(), "duplicate refuses before factory without faulting");
        check(f.manager->poll_track(7,1) == odh::NativeHitPoll::progressed &&
            f.play("alias",2,2).status == odh::HitTrackPlayStatus::dispatched,
            "existing controller remains poll/play-capable after duplicate registration refusal");
        f.manager->stop_track(7);
        check(!f.state().active && !f.manager->faulted(), "stop after duplicate registration refusal");
    });
    run("factory-exception-latches-after-callback-start", [] {
        Fixture f; f.factory_hook = [] { throw odh::FormatError("factory failed"); };
        rejects([&]{f.register_track();},"factory failed");
        check(f.manager->faulted() && f.factories == 1 && !f.manager->registered_count(),
            "factory began and failed, so manager fault remains latched");
        rejects([&]{f.play();},"faulted");
    });
    for (const auto stage : {0,1,2,3}) run(("swallowed-reentry-stage-"+std::to_string(stage)).c_str(), [stage] {
        Fixture f;
        const auto reenter = [&] { rejects([&]{f.manager->stop_track(7);},"reentered"); };
        if (stage == 0) f.factory_hook = reenter;
        else f.register_track();
        if (stage == 1) f.admission_hook = reenter;
        if (stage == 2) f.source_hook = reenter;
        if (stage == 3) f.group_hook = reenter;
        rejects([&]{ if (!stage) f.register_track(); else f.play(); },"faulted");
        check(f.manager->faulted(), "swallowed callback reentry still faults manager");
        rejects([&]{f.manager->remove_source(1,false);},"faulted");
    });
    for (const auto stage : {0,1,2}) run(("callback-exception-stage-"+std::to_string(stage)).c_str(), [stage] {
        Fixture f; f.register_track();
        const auto fail = [] { throw odh::FormatError("synthetic provider failure"); };
        if (stage == 0) f.admission_hook = fail;
        if (stage == 1) f.source_hook = fail;
        if (stage == 2) f.group_hook = fail;
        rejects([&]{f.play();},"synthetic provider failure");
        check(f.manager->faulted() && f.manager->source_count(7) == (stage == 2 ? 1U : 0U),
              "exception stops after documented completed effects");
    });
    run("swallowed-manager-reentry-in-delegated-controller-operation", [] {
        Fixture f; f.register_track(); f.play();
        f.audio_hook = [&] { rejects([&]{f.manager->stop_track(7);},"reentered"); };
        rejects([&]{f.play("one",2);},"faulted");
        check(f.manager->faulted() && f.state().controls_17_47[19-17] == 300 &&
            f.state().controls_17_47[20-17] == 768,
            "delegated operation may complete before manager observes reentry fault");
    });
    run("remaining-source-unavailable-after-erase-latches-partial-state", [] {
        Fixture f; f.register_track(); f.play(); f.play("one",2); f.samples.erase(2);
        rejects([&]{f.manager->remove_source(1,false);},"remaining positive source unavailable");
        check(f.manager->faulted() && !f.manager->contains_source(7,1) && f.manager->contains_source(7,2),
            "documented failure retains already completed erase");
    });
    std::cout << "TOTAL cases=" << cases << " pass=" << cases-failed << " fail=" << failed
              << " checks=" << checks << '\n';
    return failed ? 1 : 0;
}
