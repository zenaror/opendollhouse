#include "odh/hit_native_controller.hpp"
#include "odh/hit_patch_registry.hpp"
#include "odh/hit_resource_bank.hpp"

#include <iostream>
#include <memory>
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
        "[Patch]\n90=0,global,\"global.xa\",0,0\n"
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
    void raw_control(std::uint8_t, std::int32_t) override {}
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
    odh::NativeHitAudio audio() {
        odh::NativeHitAudio result{};
        result.active = [this] { return registry.active(binding.view()); };
        result.note_on = [this](std::uint8_t value) { registry.note_on(binding.view(), value); };
        result.stop_current = [this] { registry.stop(binding.view()); };
        result.release_binding = [this] { release(); };
        result.pause_current = [this] { registry.pause(binding.view()); };
        result.resume_current = [this] { registry.resume(binding.view()); };
        result.raw_control = [this](std::uint8_t id, std::int32_t value) {
            registry.raw_control(binding.view(), id, value);
        };
        result.bind_global_patch = [this](const odh::NativeHitPatchRef& patch,
                                         const odh::NativeHitState& state) {
            require(patch.group == 3 && patch.patch->filename == "global.xa",
                    "controller receives winning Patch D instead of Track HOT B's shadow");
            require(!binding, "old binding is released before acquiring a new one");
            auto acquired = registry.acquire(patch.id);
            if (fail_first_phase) throw odh::FormatError("synthetic first-phase failure");
            phases.push_back(state.controls_17_47[26 - 17]);
            binding = std::move(acquired); // Commit only after the phase can no longer throw.
            return std::int32_t{0}; // Explicit synthetic Patch runtime ID26.
        };
        result.apply_global_controls = [this](const odh::NativeHitState& state) {
            phases.push_back(state.controls_17_47[26 - 17]);
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
        *two.track.object.track, two.track.entry.pc, {{2,0,0}}, 100, 512,
        b.audio(), limits, overrides, {}, {}, source);
    require(generations.empty(), "constructing controllers acquires no backend");
    first.activate(1); a.resource = registry.load_resources(a.binding);
    require(first.poll(1) == odh::NativeHitPoll::progressed && generations[0]->playing,
            "NoteOn and WaitSamp act on the shared registry backend");
    const auto old_view = a.binding.view();
    second.activate(2); b.resource = registry.load_resources(b.binding);
    require(generations.size() == 1 && registry.usage().bindings == 2 &&
            registry.usage().resource_refs == 2 && generations[0]->loads == 1 &&
            !generations[0]->playing,
            "second binding reuses generation; its initializer pause stops first's playback");
    require(a.phases == std::vector<std::int32_t>{7,0} && a.phases == b.phases &&
            first.state().controls_17_47[26 - 17] == 0,
            "bind phases observe controller ID26 then effective Patch ID26");
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
    std::cout << checks << " global Patch/controller/registry checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
} }
