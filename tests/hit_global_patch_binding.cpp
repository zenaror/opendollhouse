#include "odh/hit_native_controller.hpp"
#include "odh/hit_patch_registry.hpp"
#include "odh/hit_resource_bank.hpp"

#include <iostream>
#include <memory>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool value, const char* reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, "expected FormatError for invalid operation");
}
std::string hit() {
    std::string bytes(16, '\0');
    bytes.replace(0, 4, "HIT!"); bytes[4] = 1; bytes[8] = 8;
    bytes.replace(12, 4, "TRAX");
    // Interrupt=1, NoteOn literal17, WaitSamp, End. Restart is visible without
    // inventing ownership for the literal or a scheduler Clock conversion.
    for (const int b : {5,55,1,0,0,0,2,17,11,12})
        bytes.push_back(static_cast<char>(b));
    return bytes;
}
std::unique_ptr<odh::HitResourceBank> bank() {
    const std::vector<std::string> hot{
        "[EventMapping]\none=1,7\ntwo=1,8\n",
        "[Track]\n7=0,one,1024,2,0,0,20,300,90\n"
        "8=0,two,1024,2,0,0,20,301,90\n"
        "[Patch]\n90=0,shadow,\"local.xa\",0,0\n",
        "[TrackData]\n300=16\n301=16\n",
        "[Patch]\n90=0,global,\"global.xa\",0,0,0,0,0\n"
    };
    std::vector<std::unique_ptr<std::istringstream>> streams;
    std::vector<odh::HitResourceGroupInput> input;
    auto view = [&](const std::string& text) {
        auto stream = std::make_unique<std::istringstream>(text);
        const odh::View result(*stream, text.size());
        streams.push_back(std::move(stream)); return result;
    };
    for (const auto& text : hot) input.push_back({view(""), view(hit()), view(text)});
    return std::make_unique<odh::HitResourceBank>(input);
}
struct Trace {
    bool playing = false;
    unsigned notes = 0, stops = 0, loads = 0, clears = 0, destroyed = 0;
    unsigned placement_calls = 0, gain_calls = 0, pitch_calls = 0;
    std::int32_t gain = 0, pan = 0, pitch = 0;
    std::array<std::int32_t,3> position{};
    std::vector<std::string> controls;
};
class Backend final : public odh::NativeHitPatchBackend {
public:
    explicit Backend(std::shared_ptr<Trace> trace) : trace_(std::move(trace)) {}
    ~Backend() noexcept override { ++trace_->destroyed; }
    void load_resources() override { ++trace_->loads; }
    void clear_resources() override { ++trace_->clears; }
    bool active() const override { return trace_->playing; }
    void note_on() override { ++trace_->notes; trace_->playing = true; }
    void stop() override { ++trace_->stops; trace_->playing = false; }
    void raw_control(std::uint8_t, std::int32_t) override {
        throw odh::FormatError("numeric controls must not handle global writes");
    }
    void set_pan(std::int32_t raw) override {
        ++trace_->placement_calls; trace_->pan = raw; trace_->controls.push_back("pan");
    }
    void set_position(const std::array<std::int32_t,3>& raw) override {
        ++trace_->placement_calls; trace_->position = raw; trace_->controls.push_back("position");
    }
    void set_gain(std::int32_t raw) override {
        ++trace_->gain_calls; trace_->gain = raw; trace_->controls.push_back("gain");
    }
    void set_pitch(std::int32_t raw) override {
        ++trace_->pitch_calls; trace_->pitch = raw; trace_->controls.push_back("pitch");
    }
private:
    std::shared_ptr<Trace> trace_;
};
struct Endpoint {
    odh::NativeHitPatchRegistry& registry;
    odh::NativeHitPatchRegistry::BindingLease binding;
    odh::NativeHitPatchRegistry::ResourceLease resource;
    std::vector<std::int32_t> phases;
    bool fail_first_phase = false;
    explicit Endpoint(odh::NativeHitPatchRegistry& r) : registry(r) { phases.reserve(16); }
    ~Endpoint() noexcept {
        if (binding) {
            try { registry.stop(binding.view()); } catch (...) {}
            release();
        }
    }
    void release() noexcept {
        const auto old = binding.view();
        binding.reset(); // Patch release precedes wrapper destruction's late Stop.
        try { registry.stop(old); } catch (...) {}
        resource.reset(); // May have been retired by final Patch release.
    }
    static odh::NativeHitSpatialGainInputs spatial(const odh::NativeHitState& state) {
        // Test-only effective getter provider: explicit fresh parent values.
        auto get = [&](unsigned id, std::int32_t parent) {
            const auto raw = state.controls_17_47[id - 17];
            return raw == std::numeric_limits<std::int32_t>::min() ? parent : raw;
        };
        return {{get(18,1024), get(19,1024), get(25,0), get(26,0),
                 {{get(30,0), get(31,0), get(32,0)}}},
                state.controls_17_47[20 - 17], std::nullopt, 0};
    }
    void apply(odh::NativeHitPatchView view, const odh::NativeHitState& state) {
        // Explicit test providers: no group, duck threshold0, effective Patch21=3600.
        // These do not claim immutable metadata or a physical DSP policy.
        registry.apply_spatial_gain(view,
            odh::compose_native_hit_spatial_gain(spatial(state)));
        registry.apply_pitch(view, odh::compose_native_hit_pitch({state, 3600}));
    }
    odh::NativeHitAudio audio() {
        odh::NativeHitAudio result{};
        result.active = [this] { return registry.active(binding.view()); };
        // Explicit zero/zero ID38 fixture providers, not production getters.
        result.global_note_on = {
            [](const odh::NativeHitState&) { return std::int32_t{0}; },
            [](std::uint8_t, std::int32_t, std::int32_t) {
                throw odh::FormatError("unexpected wrapper write in zero-ID38 fixture");
            },
            [this](std::uint8_t value) {
                registry.note_on(binding.view(), value);
                return true; // Synthetic backend's explicit result.
            },
            [] { return std::int32_t{0}; },
            [](const odh::NativeHitState&) {
                throw odh::FormatError("unexpected A in zero-ID38 fixture");
            },
            [](const odh::NativeHitState&) {
                throw odh::FormatError("unexpected B in zero-ID38 fixture");
            }
        };
        result.stop_current = [this] { registry.stop(binding.view()); };
        result.release_binding = [this] { release(); };
        result.pause_current = [this] { registry.pause(binding.view()); };
        result.resume_current = [this] { registry.resume(binding.view()); };
        result.apply_global_control_write = [this](std::uint8_t id, const odh::NativeHitState& state) {
            require(id == 19 || id == 20, "global snapshot identifies the A-only setter");
            registry.apply_spatial_gain(binding.view(),
                odh::compose_native_hit_spatial_gain(spatial(state)));
        };
        result.bind_global_patch = [this](const odh::NativeHitPatchRef& patch,
                                         const odh::NativeHitState& state) {
            require(patch.group == 3 && patch.patch->filename == "global.xa",
                    "controller receives winning Patch D instead of Track HOT B's shadow");
            require(!binding, "old binding is released before acquiring a new one");
            auto acquired = registry.acquire(patch.id);
            if (fail_first_phase) throw odh::FormatError("synthetic first-phase failure");
            require(patch.patch->native_id26.has_value(), "native HOT retains Patch ID26");
            phases.push_back(state.controls_17_47[26 - 17]);
            apply(acquired.view(), state);
            binding = std::move(acquired); // Commit only after the phase can no longer throw.
            return static_cast<std::int32_t>(*patch.patch->native_id26);
        };
        result.apply_global_controls = [this](const odh::NativeHitState& state) {
            phases.push_back(state.controls_17_47[26 - 17]);
            apply(binding.view(), state);
        };
        return result;
    }
};
}

int main() { try {
    auto resources = bank(); // Source streams have already been destroyed.
    const auto one = resources->objects().resolve_global_track_event(0, "one");
    const auto two = resources->objects().resolve_global_track_event(0, "two");
    require(one.track.object.group == 1 && one.track.entry.file == &resources->hit(2),
            "event A, Track B and HIT C remain separate after input streams die");
    std::vector<std::shared_ptr<Trace>> generations;
    odh::NativeHitPatchRegistry registry(resources->objects(),
        [&](const odh::NativeHitPatchRef& patch) {
            require(patch.group == 3 && patch.id == 90,
                    "registry factory is tied to the winning Patch metadata");
            auto trace = std::make_shared<Trace>(); generations.push_back(trace);
            return std::make_unique<Backend>(std::move(trace));
        }, {2, 8, 8});
    Endpoint a(registry), b(registry);
    odh::NativeHitLimits limits{128, 64, 32, 4};
    odh::HitNativeControllerValues overrides{}; overrides[26 - 17] = 7;
    const odh::NativeHitPatchSource source{
        odh::NativeHitPatchSource::Scope::global_catalog, &resources->objects()};
    odh::NativeHitController first(*one.track.entry.file, resources->hot(1),
        *one.track.object.track, one.track.entry.pc, {{1,0,0}}, 100, 512,
        a.audio(), limits, overrides, {}, {}, source);
    odh::NativeHitController second(*two.track.entry.file, resources->hot(1),
        *two.track.object.track, two.track.entry.pc, {{2,0,0}}, 200, 256,
        b.audio(), limits, overrides, {}, {}, source);
    require(generations.empty(), "constructing controllers acquires no backend");
    first.activate(1); a.resource = registry.load_resources(a.binding);
    require(first.poll(1) == odh::NativeHitPoll::progressed && generations[0]->playing,
            "NoteOn and WaitSamp act on the shared registry backend");
    require(generations[0]->controls == std::vector<std::string>{
                "position","gain","pitch","pan","gain","pitch","pan","gain","pan","gain"} &&
            generations[0]->gain == 100 && generations[0]->pan == 512 &&
            generations[0]->pitch == 3600 && generations[0]->pitch_calls == 2,
            "pre/post ID26 phases and A-only spatial setters preserve wrapper request order");
    const auto old_view = a.binding.view();
    second.activate(2); b.resource = registry.load_resources(b.binding);
    require(generations.size() == 1 && registry.usage().bindings == 2 &&
            registry.usage().resource_refs == 2 && generations[0]->loads == 1 &&
            !generations[0]->playing,
            "second binding reuses generation; its initializer pause stops first's playback");
    require(a.phases == std::vector<std::int32_t>{7,0} && a.phases == b.phases &&
            first.state().controls_17_47[26 - 17] == 0,
            "bind phases observe controller ID26 then effective Patch ID26");
    require(generations[0]->gain == 200 && generations[0]->pan == 256 &&
            generations[0]->pitch_calls == 4 && generations[0]->gain_calls == 8,
            "second controller overwrites controls observed through the same shared backend");
    second.poll(2); first.pause();
    const auto notes = generations[0]->notes;
    first.resume(3);
    require(!generations[0]->playing && generations[0]->notes == notes &&
            registry.usage().bindings == 2,
            "pause stops shared audio; resume does not replay it or remove bindings");
    require(second.poll(4) == odh::NativeHitPoll::stopped &&
            registry.usage().bindings == 1 && generations[0]->clears == 0,
            "ending one controller retains shared generation and other resource reference");
    require(first.poll(3) == odh::NativeHitPoll::stopped &&
            registry.usage().bindings == 0 && registry.usage().resource_refs == 0 &&
            generations[0]->clears == 1 && generations[0]->destroyed == 1,
            "last binding clears resources once and releases one backend owner");
    first.retrigger({{1,0,0}}, 100, 512, 10); first.poll(10);
    require(generations.size() == 2 && generations[1]->playing &&
            a.binding.view().generation != old_view.generation,
            "inactive retrigger creates a new generation of the same Patch");
    registry.stop(old_view);
    require(generations[1]->playing, "late old-wrapper Stop cannot stop a newer generation");
    rejects([&] { registry.active(old_view); });
    const auto before_restart = a.binding.view();
    first.retrigger({{1,0,0}}, 100, 512, 11);
    require(generations.size() == 3 && generations[1]->destroyed == 1 &&
            a.binding.view().generation != before_restart.generation,
            "active ID55 restart releases and reacquires even the same Patch ID");
    first.stop();
    require(registry.usage().bindings == 0 && !registry.usage().cleanup_failed &&
            generations[2]->destroyed == 1,
            "explicit final teardown leaves no live bindings or failed cleanup");
    Endpoint failed(registry); failed.fail_first_phase = true;
    odh::NativeHitController failed_controller(*one.track.entry.file, resources->hot(1),
        *one.track.object.track, one.track.entry.pc, {{1,0,0}}, 100, 512,
        failed.audio(), limits, overrides, {}, {}, source);
    rejects([&] { failed_controller.activate(12); });
    require(failed_controller.state().faulted && !failed_controller.state().binding &&
            !failed.binding && registry.usage().bindings == 0 &&
            generations.size() == 4 && generations[3]->destroyed == 1,
            "first-phase failure rolls back the temporary lease before the controller faults");
    Endpoint fallback(registry);
    odh::NativeHitController fallback_controller(*one.track.entry.file, resources->hot(1),
        *one.track.object.track, one.track.entry.pc, {{1,0,0}},
        std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min(),
        fallback.audio(), limits, overrides, {}, {}, source);
    fallback_controller.activate(13);
    require(fallback_controller.state().controls_17_47[19 - 17] ==
                std::numeric_limits<std::int32_t>::min() &&
            generations.back()->gain == 1024 && generations.back()->pan ==
                std::numeric_limits<std::int32_t>::min(),
            "effective getter fallback changes gain while raw local pan remains sentinel");
    fallback_controller.stop();
    require(registry.usage().live_patches == 0 && !registry.usage().cleanup_failed,
            "fallback control case releases its shared generation");
    std::cout << checks << " global Patch/controller/registry checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
} }
