#include "odh/hit_patch_registry.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F&& action) {
    bool rejected = false;
    try { action(); } catch (const odh::FormatError&) { rejected = true; }
    require(rejected, "registry must reject invalid operation");
}
template<class F> void throws(F&& action) {
    bool rejected = false;
    try { action(); } catch (...) { rejected = true; }
    require(rejected, "backend failure must propagate");
}

struct CatalogFixture {
    odh::HitFile hit;
    odh::Hot hot;
    std::vector<odh::HitEntryGroup> groups;
    std::unique_ptr<odh::HitEntryCatalog> entries;
    std::unique_ptr<odh::NativeHitObjectCatalog> objects;

    explicit CatalogFixture(bool track_wins = false) {
        hit.major_version = 1;
        hit.minor_version = 8;
        hit.bytes.resize(32);
        hot.patches[10] = {"first", "first.xa", false, false};
        hot.patches[20] = {"second", "second.xa", false, false};
        hot.native_object_order = std::vector<odh::HotObjectRegistration>{
            {odh::HotObjectKind::patch, 10, 1},
            {odh::HotObjectKind::patch, 20, 2}
        };
        if (track_wins) {
            hot.tracks[10] = {};
            hot.native_object_order->push_back({odh::HotObjectKind::track, 10, 3});
        }
        groups.push_back({&hit, &hot});
        entries = std::make_unique<odh::HitEntryCatalog>(groups);
        objects = std::make_unique<odh::NativeHitObjectCatalog>(groups, *entries);
    }
};

struct Recorder {
    std::vector<std::string> events;
    unsigned created = 0;
    unsigned destroyed = 0;
    bool throw_factory = false;
    bool null_factory = false;
    bool throw_load = false;
    bool throw_clear = false;
    std::function<void()> on_factory, on_load, on_note, on_stop, on_pan;

    unsigned count(const std::string& event) const {
        unsigned result = 0;
        for (const auto& item : events) if (item == event) ++result;
        return result;
    }
};

class Backend final : public odh::NativeHitPatchBackend {
public:
    Backend(Recorder& recorder, std::uint32_t patch)
        : recorder_(recorder), id_(++recorder.created), patch_(patch) {
        recorder_.events.push_back("create:" + std::to_string(id_) + ":" +
                                   std::to_string(patch_));
    }
    ~Backend() noexcept override {
        ++recorder_.destroyed;
        recorder_.events.push_back("destroy:" + std::to_string(id_));
    }
    void load_resources() override {
        recorder_.events.push_back("load:" + std::to_string(id_));
        if (recorder_.on_load) recorder_.on_load();
        if (recorder_.throw_load) throw std::runtime_error("load failed");
    }
    void clear_resources() override {
        recorder_.events.push_back("clear:" + std::to_string(id_));
        if (recorder_.throw_clear) throw std::runtime_error("clear failed");
    }
    bool active() const override { return playing_; }
    void note_on() override {
        recorder_.events.push_back("note:" + std::to_string(id_));
        playing_ = true;
        if (recorder_.on_note) recorder_.on_note();
    }
    void stop() override {
        recorder_.events.push_back("stop:" + std::to_string(id_));
        playing_ = false;
        if (recorder_.on_stop) recorder_.on_stop();
    }
    void raw_control(std::uint8_t id, std::int32_t raw) override {
        recorder_.events.push_back("raw:" + std::to_string(id_) + ":" +
                                   std::to_string(id) + ":" + std::to_string(raw));
    }
    void set_pan(std::int32_t raw) override {
        recorder_.events.push_back("pan:" + std::to_string(raw));
        if (recorder_.on_pan) recorder_.on_pan();
    }
    void set_position(const std::array<std::int32_t,3>& raw) override {
        recorder_.events.push_back("position:" + std::to_string(raw[0]) + ":" +
                                   std::to_string(raw[1]) + ":" + std::to_string(raw[2]));
    }
    void set_gain(std::int32_t raw) override {
        recorder_.events.push_back("gain:" + std::to_string(raw));
    }
    void set_pitch(std::int32_t raw) override {
        recorder_.events.push_back("pitch:" + std::to_string(raw));
    }
private:
    Recorder& recorder_;
    unsigned id_;
    std::uint32_t patch_;
    bool playing_ = false;
};

odh::NativeHitPatchBackendFactory factory(Recorder& recorder) {
    return [&recorder](const odh::NativeHitPatchRef& patch)
            -> std::unique_ptr<odh::NativeHitPatchBackend> {
        if (recorder.throw_factory) throw std::runtime_error("factory failed");
        if (recorder.null_factory) return nullptr;
        auto backend = std::make_unique<Backend>(recorder, patch.id);
        if (recorder.on_factory) recorder.on_factory();
        return backend;
    };
}
}

int main() { try {
    static_assert(!std::is_copy_constructible_v<odh::NativeHitPatchRegistry::BindingLease>);
    static_assert(!std::is_copy_constructible_v<odh::NativeHitPatchRegistry::ResourceLease>);
    static_assert(std::is_nothrow_destructible_v<odh::NativeHitPatchRegistry::BindingLease>);
    static_assert(std::is_nothrow_destructible_v<odh::NativeHitPatchRegistry::ResourceLease>);

    CatalogFixture fixture;
    Recorder recorder;
    odh::NativeHitPatchRegistry registry(*fixture.objects, factory(recorder));
    auto first = registry.acquire(10);
    const auto old_view = first.view();
    auto second = registry.acquire(10);
    require(recorder.created == 1 && first.view().generation == second.view().generation &&
            registry.usage().bindings == 2 && registry.usage().live_patches == 1,
            "two bindings to the same Patch share one backend generation");
    require(recorder.count("load:1") == 0,
            "binding does not implicitly load resources");
    auto resource1 = registry.load_resources(first);
    auto resource2 = registry.load_resources(second);
    require(recorder.count("load:1") == 1 && registry.usage().resource_refs == 2,
            "two resource leases call backend load only once");
    registry.note_on(first.view(), 9);
    require(registry.active(second.view()) && recorder.count("note:1") == 1,
            "NoteOn acts on the shared backend without allocating another one");
    registry.pause(second.view());
    require(!registry.active(first.view()) && recorder.count("stop:1") == 1,
            "pause forwards Stop to the shared backend");
    registry.resume(first.view());
    require(!registry.active(second.view()) && recorder.count("note:1") == 1,
            "resume performs no backend audio work");
    registry.raw_control(first.view(), 19, -123);
    require(recorder.count("raw:1:19:-123") == 1,
            "raw control is forwarded without numeric conversion");
    registry.note_on(second.view(), 255);
    first.reset();
    first.reset();
    auto same_generation_rebind = registry.acquire(10);
    require(recorder.created == 1 &&
            same_generation_rebind.view().generation == second.view().generation &&
            registry.usage().bindings == 2,
            "same-ID rebind reuses backend only while another binding retains Patch");
    same_generation_rebind.reset();
    registry.stop(old_view);
    require(!registry.active(second.view()) && registry.usage().bindings == 1 &&
            recorder.destroyed == 0,
            "stop after one binding release still stops the shared backend");
    resource1.reset();
    require(recorder.count("clear:1") == 0 && registry.usage().resource_refs == 1,
            "resource clear waits for its last lease");
    resource2.reset();
    resource2.reset();
    require(recorder.count("clear:1") == 1 && registry.usage().resource_refs == 0,
            "last resource release clears exactly once");
    second.reset();
    require(recorder.destroyed == 1 && registry.usage().live_patches == 0,
            "last binding release destroys the one backend owner");
    registry.stop(old_view);
    rejects([&] { registry.active(old_view); });
    rejects([&] { registry.note_on(old_view, 1); });
    rejects([&] { registry.pause(old_view); });
    rejects([&] { registry.resume(old_view); });
    rejects([&] { registry.raw_control(old_view, 19, 1); });
    require(recorder.count("stop:1") == 2,
            "late wrapper stop after final release does not touch retired backend");
    auto rebound = registry.acquire(10);
    require(recorder.created == 2 && rebound.view().generation != old_view.generation &&
            registry.usage().generations_created == 2,
            "same-ID rebind after final release creates a new generation");
    registry.note_on(rebound.view(), 1);
    registry.stop(old_view);
    require(registry.active(rebound.view()),
            "stale view cannot stop a new backend in a reused slot");
    auto lingering_resource = registry.load_resources(rebound);
    rebound.reset();
    require(recorder.count("clear:2") == 1 && recorder.count("destroy:2") == 1 &&
            registry.usage().resource_refs == 0,
            "final Patch release clears outstanding resources before teardown");
    require(static_cast<bool>(lingering_resource),
            "retired resource lease retains a safe release token, not live resources");
    lingering_resource.reset();
    require(recorder.count("clear:2") == 1,
            "resource lease retired by final Patch release cannot clear twice");

    CatalogFixture track_wins(true);
    Recorder wrong_kind;
    odh::NativeHitPatchRegistry wrong_registry(*track_wins.objects, factory(wrong_kind));
    rejects([&] { wrong_registry.acquire(10); });
    rejects([&] { wrong_registry.stop(old_view); });
    require(wrong_kind.created == 0,
            "Track winner blocks Patch acquisition with no local fallback");

    Recorder bounded;
    rejects([&] { odh::NativeHitPatchRegistry over_budget(
        *fixture.objects, factory(bounded), {65537, 1, 1}); });
    odh::NativeHitPatchRegistry limited(*fixture.objects, factory(bounded), {1, 2, 1});
    auto bound1 = limited.acquire(10);
    auto bound2 = limited.acquire(10);
    rejects([&] { limited.acquire(10); });
    rejects([&] { limited.acquire(20); });
    auto loaded = limited.load_resources(bound1);
    rejects([&] { limited.load_resources(bound2); });
    require(bounded.created == 1 && limited.usage().bindings == 2 &&
            limited.usage().resource_refs == 1,
            "capacity rejection leaves active backend and counters unchanged");
    loaded.reset();
    bound2.reset();
    bound1.reset();
    auto other_patch = limited.acquire(20);
    require(bounded.created == 2 && other_patch.view().generation != 0,
            "released fixed slot can be reused by another Patch");
    other_patch.reset();

    Recorder failing_factory;
    odh::NativeHitPatchRegistry transactional(*fixture.objects, factory(failing_factory));
    failing_factory.throw_factory = true;
    throws([&] { transactional.acquire(10); });
    failing_factory.throw_factory = false;
    failing_factory.null_factory = true;
    rejects([&] { transactional.acquire(10); });
    failing_factory.null_factory = false;
    require(transactional.usage().bindings == 0 &&
            transactional.usage().generations_created == 0,
            "factory throw and null result do not publish a binding or generation");
    auto recovered = transactional.acquire(10);
    require(recovered.view().generation == 1,
            "first successful factory commit receives first generation");
    recovered.reset();

    Recorder factory_reentry;
    odh::NativeHitPatchRegistry reentrant_factory(
        *fixture.objects, factory(factory_reentry));
    bool factory_reentry_caught = false;
    factory_reentry.on_factory = [&] {
        try { static_cast<void>(reentrant_factory.usage()); }
        catch (const odh::FormatError&) { factory_reentry_caught = true; }
    };
    rejects([&] { reentrant_factory.acquire(10); });
    require(factory_reentry_caught && factory_reentry.created == 1 &&
            factory_reentry.destroyed == 1 && reentrant_factory.usage().bindings == 0 &&
            !reentrant_factory.usage().cleanup_failed,
            "swallowed factory reentry rejects commit and RAII destroys candidate");

    Recorder operation_reentry;
    odh::NativeHitPatchRegistry reentrant_operation(
        *fixture.objects, factory(operation_reentry));
    auto operation_binding = reentrant_operation.acquire(10);
    const auto operation_view = operation_binding.view();
    bool operation_reentry_caught = false;
    operation_reentry.on_note = [&] {
        try { static_cast<void>(reentrant_operation.active(operation_view)); }
        catch (const odh::FormatError&) { operation_reentry_caught = true; }
    };
    rejects([&] { reentrant_operation.note_on(operation_view, 2); });
    require(operation_reentry_caught && reentrant_operation.usage().bindings == 1 &&
            !reentrant_operation.usage().cleanup_failed,
            "swallowed backend reentry does not alter binding ownership");
    operation_reentry.on_note = [&] { operation_binding.reset(); };
    rejects([&] { reentrant_operation.note_on(operation_view, 3); });
    require(!operation_binding && reentrant_operation.usage().bindings == 0 &&
            operation_reentry.destroyed == 1,
            "lease destruction during callback is deferred until callback returns");

    Recorder failing_stop;
    odh::NativeHitPatchRegistry stop_registry(*fixture.objects, factory(failing_stop));
    auto stop_binding = stop_registry.acquire(10);
    failing_stop.on_stop = [] { throw std::runtime_error("stop failed"); };
    throws([&] { stop_registry.pause(stop_binding.view()); });
    require(stop_registry.usage().bindings == 1,
            "backend stop failure preserves the binding count");
    failing_stop.on_stop = {};
    stop_binding.reset();

    Recorder failing_load;
    odh::NativeHitPatchRegistry load_registry(*fixture.objects, factory(failing_load));
    auto load_binding = load_registry.acquire(10);
    failing_load.throw_load = true;
    throws([&] { load_registry.load_resources(load_binding); });
    require(failing_load.count("load:1") == 1 && failing_load.count("clear:1") == 1 &&
            load_registry.usage().resource_refs == 0,
            "failed resource load rolls back and leaves no resource lease");
    load_binding.reset();

    Recorder load_reentry;
    odh::NativeHitPatchRegistry load_reentry_registry(
        *fixture.objects, factory(load_reentry));
    auto load_reentry_binding = load_reentry_registry.acquire(10);
    bool load_reentry_caught = false;
    load_reentry.on_load = [&] {
        try { static_cast<void>(load_reentry_registry.usage()); }
        catch (const odh::FormatError&) { load_reentry_caught = true; }
    };
    rejects([&] { load_reentry_registry.load_resources(load_reentry_binding); });
    require(load_reentry_caught && load_reentry_registry.usage().resource_refs == 0 &&
            load_reentry.count("clear:1") == 1,
            "swallowed load reentry rolls back without publishing resource ref");
    load_reentry_binding.reset();

    Recorder failing_clear;
    odh::NativeHitPatchRegistry clear_registry(*fixture.objects, factory(failing_clear));
    auto clear_binding = clear_registry.acquire(10);
    auto clear_lease = clear_registry.load_resources(clear_binding);
    failing_clear.throw_clear = true;
    clear_lease.reset();
    clear_lease.reset();
    require(clear_registry.usage().cleanup_failed &&
            clear_registry.usage().resource_refs == 0 &&
            failing_clear.count("clear:1") == 1,
            "throwing resource cleanup is noexcept, idempotent and reported");
    rejects([&] { clear_registry.load_resources(clear_binding); });
    clear_binding.reset();
    require(failing_clear.destroyed == 1 && failing_clear.count("clear:1") == 1,
            "faulted backend is destroyed once without duplicate clear");

    Recorder failing_final_clear;
    odh::NativeHitPatchRegistry final_clear_registry(
        *fixture.objects, factory(failing_final_clear));
    auto final_binding = final_clear_registry.acquire(10);
    auto final_resource = final_clear_registry.load_resources(final_binding);
    failing_final_clear.throw_clear = true;
    final_binding.reset();
    require(final_clear_registry.usage().cleanup_failed &&
            final_clear_registry.usage().live_patches == 0 &&
            failing_final_clear.destroyed == 1 &&
            failing_final_clear.count("clear:1") == 1,
            "final binding teardown catches clear failure and destroys backend once");
    final_resource.reset();
    require(failing_final_clear.count("clear:1") == 1,
            "retired resource lease does not repeat failed final clear");

    Recorder retired_tokens;
    odh::NativeHitPatchRegistry tiny(*fixture.objects, factory(retired_tokens),
                                     {1, 1, 1});
    std::vector<odh::NativeHitPatchRegistry::ResourceLease> old_resources;
    for (unsigned i = 0; i < 4; ++i) {
        auto temporary = tiny.acquire(10);
        old_resources.push_back(tiny.load_resources(temporary));
        temporary.reset(); // Retires the resource token without resetting it.
    }
    auto current_binding = tiny.acquire(10);
    const auto current_view = current_binding.view();
    retired_tokens.on_note = [&] {
        for (auto& old : old_resources) old.reset();
        current_binding.reset();
    };
    rejects([&] { tiny.note_on(current_view, 1); });
    require(tiny.usage().bindings == 0 && tiny.usage().live_patches == 0 &&
            !tiny.usage().cleanup_failed &&
            retired_tokens.destroyed == 5,
            "retired tokens cannot fill pending queue and drop a live binding release");

    Recorder controls;
    odh::NativeHitPatchRegistry control_registry(*fixture.objects, factory(controls));
    auto control_binding = control_registry.acquire(10);
    const auto control_view = control_binding.view();
    controls.events.clear();
    const odh::NativeHitSpatialGainRequest pan_request{odh::NativeHitPlacement::pan, -3, 512, {{4,5,6}}};
    control_registry.apply_spatial_gain(control_view, pan_request);
    control_registry.apply_pitch(control_view, {-500});
    control_registry.apply_spatial_gain(control_view,
        {odh::NativeHitPlacement::position, 2048, 999, {{-1,2,3}}});
    require(controls.events == std::vector<std::string>{
        "pan:512", "gain:-3", "pitch:-500", "position:-1:2:3", "gain:2048"},
        "typed wrapper controls retain raw values and placement-before-gain order");
    const auto control_count = controls.events.size();
    rejects([&] { control_registry.apply_spatial_gain(control_view,
        {static_cast<odh::NativeHitPlacement>(99), 0, 0, {{0,0,0}}}); });
    require(controls.events.size() == control_count, "invalid placement has no backend effect");
    controls.on_pan = [] { throw std::runtime_error("pan failed"); };
    throws([&] { control_registry.apply_spatial_gain(control_view, pan_request); });
    require(controls.events.back() == "pan:512" && controls.events.size() == control_count+1,
        "placement failure prevents gain from being issued");
    controls.on_pan = [&] {
        try { control_registry.apply_pitch(control_view, {77}); }
        catch (const odh::FormatError&) {}
    };
    rejects([&] { control_registry.apply_spatial_gain(control_view, pan_request); });
    require(controls.events.back() == "pan:512" && controls.events.size() == control_count+2,
        "swallowed placement callback reentry prevents the subsequent gain call");
    controls.on_pan = {};
    control_binding.reset();
    rejects([&] { control_registry.apply_spatial_gain(control_view, pan_request); });
    rejects([&] { control_registry.apply_pitch(control_view, {77}); });

    std::cout << "hit patch registry: " << checks << " checks, sizeof(view)="
              << sizeof(odh::NativeHitPatchView) << ", sizeof(registry)="
              << sizeof(odh::NativeHitPatchRegistry) << ", fixed default storage="
              << registry.fixed_storage_bytes() << " bytes\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "hit patch registry: " << error.what() << '\n';
    return 1;
} }
