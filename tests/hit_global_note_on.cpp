#include "odh/hit_native_controller.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* why) {
    if (!value) throw std::runtime_error(why);
}
odh::HitFile hit(std::uint8_t literal, bool with_note = true) {
    odh::HitFile result;
    result.major_version = 1;
    result.minor_version = 8;
    result.bytes.resize(16);
    if (with_note) {
        result.bytes.push_back(2);
        result.bytes.push_back(literal);
    }
    result.bytes.push_back(9); // Wait using R1=1.
    result.bytes.push_back(1);
    return result;
}
odh::Hot hot() {
    const std::string text =
        "[Track]\n1=0,matrix,1024,2,0,0,20,300,100\n"
        "[TrackData]\n300=16\n"
        "[Patch]\n100=0,matrix,\"synthetic.xa\",0,0,0,0,0\n";
    std::istringstream stream(text);
    return odh::read_hot(odh::View(stream, text.size()), nullptr, {},
                         odh::HotReadProfile::native_entry_map);
}
struct Fixture {
    odh::HitFile code;
    odh::Hot metadata;
    odh::HotTrack track;
    std::vector<odh::HitEntryGroup> groups;
    odh::HitEntryCatalog entries;
    odh::NativeHitObjectCatalog objects;
    explicit Fixture(std::uint8_t literal = 17, bool with_note = true)
        : code(hit(literal, with_note)), metadata(hot()),
          track(metadata.tracks.at(1)), groups{{&code, &metadata}},
          entries(groups), objects(groups, entries) {}
    odh::NativeHitPatchSource source() const {
        return {odh::NativeHitPatchSource::Scope::global_catalog, &objects};
    }
};
enum class Fault { none, exception, swallowed_reentry };
constexpr std::array<const char*, 6> stages{{
    "controller38", "wrapper_write", "wrapper_note", "patch38", "A", "B"
}};
struct Provider {
    std::int32_t effective_controller38 = 0;
    std::int32_t raw_controller38 = 0;
    std::int32_t patch_before = 0, patch_after = 0;
    const std::int32_t* current_patch = &patch_before;
    bool play_result = true;
    bool a_done = false;
    unsigned literal_seen = 0, writes = 0, stops = 0, releases = 0;
    std::vector<std::string> events;
    odh::NativeHitController* controller = nullptr;
    Fault fault = Fault::none;
    std::string fault_stage;

    void stage(const char* name) {
        events.emplace_back(name);
        if (fault_stage != name) return;
        if (fault == Fault::exception) throw std::runtime_error("provider failure");
        if (fault == Fault::swallowed_reentry) {
            check(controller != nullptr, "reentry fixture has a controller");
            try { controller->pause(); } catch (const odh::FormatError&) {}
        }
    }
    void snapshot(const odh::NativeHitState& state) const {
        check(state.binding && state.active && !state.paused && state.pc == 18 &&
              state.instructions == 1, "NoteOn snapshot is the current bound instruction");
        check(state.controls_17_47[38 - 17] == raw_controller38,
              "provider receives raw controller bank without replacing public getter");
    }
    odh::NativeHitAudio audio() {
        odh::NativeHitAudio result;
        result.bind = [](std::uint32_t) {};
        result.active = [] { return false; };
        result.note_on = [](std::uint8_t) {
            throw std::runtime_error("global NoteOn must not use the local fallback");
        };
        result.stop_current = [this] { ++stops; };
        result.release_binding = [this] { ++releases; };
        result.pause_current = [] {};
        result.resume_current = [] {};
        result.raw_control = [](std::uint8_t, std::int32_t) {};
        result.bind_global_patch =
            [](const odh::NativeHitPatchRef& ref, const odh::NativeHitState&) {
                check(ref.id == 100, "fixture binds the catalog Patch");
                return std::int32_t{0};
            };
        result.apply_global_controls = [](const odh::NativeHitState&) {};
        result.apply_global_control_write = [](std::uint8_t, const odh::NativeHitState&) {};
        result.global_note_on = {
            [this](const odh::NativeHitState& state) {
                snapshot(state);
                stage("controller38");
                return effective_controller38;
            },
            [this](std::uint8_t id, std::int32_t value, std::int32_t argument) {
                check(id == 38 && value == 1 && argument == 0,
                      "wrapper receives exactly native writer(38,1,0)");
                stage("wrapper_write");
                ++writes;
            },
            [this](std::uint8_t literal) {
                stage("wrapper_note");
                literal_seen = literal;
                // Simulate changing the current Patch provider during play.
                // The later getter must consult this new live source.
                current_patch = &patch_after;
                return play_result;
            },
            [this] {
                stage("patch38");
                check(current_patch == &patch_after, "Patch getter occurs after wrapper play");
                return *current_patch;
            },
            [this](const odh::NativeHitState& state) {
                snapshot(state);
                stage("A");
                a_done = true;
            },
            [this](const odh::NativeHitState& state) {
                snapshot(state);
                check(a_done, "B observes A's external provider effect");
                stage("B");
            }
        };
        return result;
    }
    odh::HitNativeControllerValues overrides() const {
        odh::HitNativeControllerValues result{};
        result[38 - 17] = raw_controller38;
        return result;
    }
};
std::vector<std::string> expected(std::int32_t controller38, std::int32_t patch38) {
    if (controller38 == 0 && patch38 == 0)
        return {"controller38", "wrapper_note", "patch38"};
    if (controller38 != 0 && patch38 == 0)
        return {"controller38", "wrapper_write", "wrapper_note", "patch38"};
    if (controller38 == 0)
        return {"controller38", "wrapper_note", "patch38", "A", "B"};
    return {"controller38", "wrapper_write", "wrapper_note", "patch38", "A", "B"};
}
void matrix(std::int32_t controller38, std::int32_t patch38, bool play,
            std::uint8_t literal = 17) {
    Fixture fixture(literal);
    Provider provider;
    provider.effective_controller38 = controller38;
    // Deliberately disagree with the public getter. This proves the runtime
    // does not replace the provider's public getter with its raw read(38).
    provider.raw_controller38 = controller38 == 0 ? 9 : 0;
    provider.patch_before = patch38 == 0 ? 21 : 0;
    provider.patch_after = patch38;
    provider.play_result = play;
    auto audio = provider.audio();
    audio.note_on = {}; // Global mode has no requirement for local NoteOn.
    odh::NativeHitController controller(fixture.code, fixture.metadata, fixture.track,
        16, {{1,200,300}}, 100, 512, audio, {}, provider.overrides(), {}, {}, fixture.source());
    provider.controller = &controller;
    controller.activate(1);
    const auto before = controller.state();
    check(provider.events.empty(), "activation does not run the NoteOn matrix");
    check(controller.poll(1) == odh::NativeHitPoll::progressed, "NoteOn advances to Wait");
    check(provider.events == expected(controller38, patch38), "exact native callback order");
    check(provider.literal_seen == literal, "literal reaches wrapper without handle interpretation");
    check(provider.writes == (controller38 != 0 ? 1U : 0U),
          "only effective controller38 gates the wrapper writer");
    check(controller.state().persistent == before.persistent &&
          controller.state().controls_17_47 == before.controls_17_47 &&
          controller.state().controls_50_62 == before.controls_50_62 &&
          controller.state().zero == before.zero &&
          controller.state().positive == before.positive &&
          controller.state().sign == before.sign,
          "NoteOn literal and play result do not write controller variables or flags");
    check(controller.trace().size() == 2 && controller.trace()[0].opcode == 2 &&
          controller.trace()[0].next_pc == 18 && controller.trace()[1].opcode == 9 &&
          controller.state().deadline == 2, "NoteOn traces and Wait deadline are preserved");
}
void clear_callback(odh::NativeHitGlobalNoteOn& note, std::size_t stage) {
    switch (stage) {
    case 0: note.controller_id38 = {}; break;
    case 1: note.wrapper_write = {}; break;
    case 2: note.wrapper_note_on = {}; break;
    case 3: note.patch_id38 = {}; break;
    case 4: note.controller_a = {}; break;
    case 5: note.controller_b = {}; break;
    default: throw std::runtime_error("bad missing-callback fixture index");
    }
}
void missing(std::size_t stage) {
    Fixture fixture;
    Provider provider;
    auto audio = provider.audio();
    clear_callback(audio.global_note_on, stage);
    odh::NativeHitController controller(fixture.code, fixture.metadata, fixture.track,
        16, {{1,0,0}}, 100, 512, audio, {}, {}, {}, {}, fixture.source());
    controller.activate(1);
    bool rejected = false;
    try { controller.poll(1); } catch (const odh::FormatError&) { rejected = true; }
    check(rejected && controller.state().faulted, "incomplete global NoteOn faults");
    check(provider.events.empty(), "all callbacks are checked before the first effect");
    check(controller.trace().empty(), "failed NoteOn has no completed instruction trace");
    check(provider.stops == 0 && provider.releases == 0 && controller.state().binding,
          "poll failure retains external cleanup responsibility");
    rejected = false;
    try { controller.resume(2); } catch (const odh::FormatError&) { rejected = true; }
    check(rejected && provider.events.empty(), "faulted controller cannot resume effects");
}
void failing(std::size_t stage_index, Fault fault) {
    Fixture fixture;
    Provider provider;
    provider.effective_controller38 = 1;
    provider.patch_after = 1;
    provider.fault = fault;
    provider.fault_stage = stages.at(stage_index);
    odh::NativeHitController controller(fixture.code, fixture.metadata, fixture.track,
        16, {{1,0,0}}, 100, 512, provider.audio(), {}, {}, {}, {}, fixture.source());
    provider.controller = &controller;
    controller.activate(1);
    bool rejected = false;
    try { controller.poll(1); } catch (const std::exception&) { rejected = true; }
    check(rejected && controller.state().faulted, "callback failure latches controller fault");
    const auto all = expected(1, 1);
    const std::vector<std::string> prefix(all.begin(),
        all.begin() + static_cast<std::ptrdiff_t>(stage_index + 1));
    check(provider.events == prefix, "callback failure stops before every later operation");
    check(controller.trace().empty() && controller.state().binding &&
          provider.stops == 0 && provider.releases == 0,
          "failed poll preserves existing trace and external teardown contract");
    rejected = false;
    try { controller.poll(2); } catch (const odh::FormatError&) { rejected = true; }
    check(rejected && provider.events == prefix, "latched fault cannot replay callbacks");
}
void no_binding(std::uint32_t patch) {
    Fixture fixture;
    fixture.track.patch_id = patch;
    Provider provider;
    auto audio = provider.audio();
    audio.global_note_on = {};
    odh::NativeHitController controller(fixture.code, fixture.metadata, fixture.track,
        16, {{1,0,0}}, 100, 512, audio, {}, {}, {}, {}, fixture.source());
    controller.activate(1);
    check(!controller.state().binding, "fixture has no acquired Patch");
    check(controller.poll(1) == odh::NativeHitPoll::progressed && provider.events.empty(),
          "unbound NoteOn has no getter, wrapper or A/B callback");
}
void no_note() {
    Fixture fixture(17, false);
    Provider provider;
    auto audio = provider.audio();
    audio.global_note_on = {};
    odh::NativeHitController controller(fixture.code, fixture.metadata, fixture.track,
        16, {{1,0,0}}, 100, 512, audio, {}, {}, {}, {}, fixture.source());
    controller.activate(1);
    check(controller.poll(1) == odh::NativeHitPoll::progressed &&
          controller.state().binding && provider.events.empty(),
          "global programs without NoteOn do not need a fictitious provider");
}
void local(bool omit_note) {
    Fixture fixture;
    Provider provider;
    auto audio = provider.audio();
    unsigned local_notes = 0;
    audio.note_on = [&](std::uint8_t value) {
        check(value == 17, "local profile keeps literal NoteOn");
        ++local_notes;
    };
    if (omit_note) audio.note_on = {};
    bool rejected = false;
    try {
        odh::NativeHitController controller(fixture.code, fixture.metadata, fixture.track,
            16, {{1,0,0}}, 100, 512, audio);
        controller.activate(1);
        check(controller.poll(1) == odh::NativeHitPoll::progressed,
              "local NoteOn continues to Wait");
    } catch (const odh::FormatError&) { rejected = true; }
    check(rejected == omit_note && local_notes == (omit_note ? 0U : 1U) &&
          provider.events.empty(), "local profile ignores global callbacks and requires its own");
}
}

int main() {
    struct Case { std::string id; std::function<void()> run; };
    std::vector<Case> cases;
    for (const std::int32_t controller38 : {0, -3})
        for (const std::int32_t patch38 : {0, -5})
            for (const bool play : {false, true})
                cases.push_back({"matrix/c" + std::to_string(controller38) +
                    "/p" + std::to_string(patch38) + "/play" + std::to_string(play),
                    [=] { matrix(controller38, patch38, play); }});
    cases.push_back({"matrix/signed-min", [] {
        matrix(std::numeric_limits<std::int32_t>::min(),
               std::numeric_limits<std::int32_t>::min(), false);
    }});
    for (const std::uint8_t literal : {std::uint8_t{0}, std::uint8_t{255}})
        cases.push_back({"literal/" + std::to_string(literal),
                         [=] { matrix(1, 1, true, literal); }});
    for (std::size_t i = 0; i < stages.size(); ++i) {
        cases.push_back({"missing/" + std::string(stages[i]), [=] { missing(i); }});
        cases.push_back({"throw/" + std::string(stages[i]),
                         [=] { failing(i, Fault::exception); }});
        cases.push_back({"reentry/" + std::string(stages[i]),
                         [=] { failing(i, Fault::swallowed_reentry); }});
    }
    cases.push_back({"binding/zero", [] { no_binding(0); }});
    cases.push_back({"binding/missing", [] { no_binding(101); }});
    cases.push_back({"opcode/not-reached", no_note});
    cases.push_back({"local/complete", [] { local(false); }});
    cases.push_back({"local/missing", [] { local(true); }});
    unsigned passed = 0, failed = 0;
    for (const auto& test : cases) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.id << '\n';
        } catch (const std::exception& error) {
            ++failed;
            std::cerr << "FAIL " << test.id << ": " << error.what() << '\n';
        }
    }
    std::cout << "global NoteOn matrix: " << passed << " passed, " << failed
              << " failed, 0 blocked, 0 not run; " << cases.size() << " total\n";
    return failed == 0 ? 0 : 1;
}
