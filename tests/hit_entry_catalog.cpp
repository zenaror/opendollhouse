#include "odh/hit_entry_catalog.hpp"
#include "odh/hit_native_controller.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const std::string& reason) {
    bool rejected = false;
    try { action(); } catch (const odh::FormatError& error) {
        rejected = std::string(error.what()).find(reason) != std::string::npos;
    }
    require(rejected, "catalog must reject unsupported input explicitly");
}
odh::HitFile hit() {
    odh::HitFile file;
    file.major_version = 1;
    file.minor_version = 8;
    file.bytes.resize(32);
    return file;
}
odh::Hot hot(std::uint64_t extent) {
    odh::Hot resource;
    resource.logical_bytes = extent;
    return resource;
}
odh::HitFile script(std::initializer_list<std::uint8_t> code) {
    auto file = hit();
    file.bytes.resize(16);
    file.bytes.insert(file.bytes.end(), code);
    return file;
}
odh::Hot parsed_hot(const std::string& text, const odh::Hsm& hsm) {
    std::istringstream stream(text);
    return odh::read_hot(odh::View(stream, text.size()), &hsm, {},
                         odh::HotReadProfile::native_entry_map);
}
}

int main() { try {
    auto first_hit = hit(), second_hit = hit();
    auto first_hot = hot(100), second_hot = hot(120);
    first_hot.track_data = {{10, 16}, {20, 17}};
    second_hot.track_data = {{10, 18}, {30, 20}};
    first_hot.events["alpha"] = {1, 7, {}};
    odh::HotTrack track;
    track.hitlist_id = 10;
    first_hot.tracks[7] = track;
    const std::vector<odh::HitEntryGroup> order{{&first_hit, &first_hot},
                                                 {&second_hit, &second_hot}};
    const odh::HitEntryCatalog catalog(order);
    require(catalog.usage().groups == 2 && catalog.usage().track_data_rows == 4 &&
            catalog.usage().unique_keys == 3 && catalog.usage().serialized_bytes == 284,
            "aggregate accounting uses serialized extents and all processed rows");
    require(catalog.lookup(10) && catalog.lookup(10)->file == &second_hit &&
            catalog.lookup(10)->pc == 18 && catalog.lookup(20)->file == &first_hit &&
            catalog.lookup(30)->file == &second_hit && !catalog.lookup(99),
            "last processed TrackData row preserves target resource identity");
    require(catalog.collisions().size() == 1 && catalog.collisions()[0].key == 10 &&
            catalog.collisions()[0].previous_group == 0 &&
            catalog.collisions()[0].replacement_group == 1 &&
            catalog.collisions()[0].previous_pc == 16 &&
            catalog.collisions()[0].replacement_pc == 18,
            "cross-group overwrite retains bounded provenance");
    const auto event = catalog.resolve_event(0, "ALPHA");
    require(event.event_group == 0 && event.event == &first_hot.events.at("alpha") &&
            event.track == &first_hot.tracks.at(7) && event.track_id == 7 &&
            event.entry_key == 10 && event.entry.file == &second_hit && event.entry.pc == 18,
            "event metadata stays with its HOT while entry can target another HIT");
    odh::HitEntryCatalog reverse({order[1], order[0]});
    require(reverse.lookup(10)->file == &first_hit && reverse.lookup(10)->pc == 16,
            "caller-supplied group order controls overwrite");

    require(!catalog.lookup(99), "HIT ENTP is not merged into native TrackData catalog");
    auto with_first_copy = [&](auto change, const std::string& reason) {
        auto file = first_hit;
        auto resource = first_hot;
        change(file, resource);
        const std::vector<odh::HitEntryGroup> modified{{&file, &resource}, order[1]};
        rejects([&] { odh::HitEntryCatalog invalid(modified); }, reason);
    };
    with_first_copy([](auto& file, auto& resource) {
        file.entry_table_offset = 20;
        resource.track_data[20] = 20;
    }, "outside paired HIT");
    with_first_copy([](auto&, auto& resource) {
        resource.track_data[20] = 15;
    }, "outside paired HIT");
    with_first_copy([](auto& file, auto&) {
        file.entry_table_offset = 33;
    }, "invalid HIT code region");
    with_first_copy([](auto& file, auto&) {
        file.major_version = 2;
    }, "unsupported HIT version");
    {
        auto file = first_hit;
        file.entry_table_offset = 20;
        file.entry_points[99] = 19;
        const odh::HitEntryCatalog with_entp({{&file, &first_hot}});
        require(with_entp.lookup(20)->pc == 17 && !with_entp.lookup(99),
                "ENTP bounds code but does not supply native map keys");
    }

    auto bounds = odh::HitEntryCatalogLimits{};
    bounds.groups = 1;
    rejects([&] { odh::HitEntryCatalog invalid(order, bounds); }, "budget");
    bounds = {}; bounds.track_data_rows = 3;
    rejects([&] { odh::HitEntryCatalog invalid(order, bounds); }, "aggregate budget");
    bounds = {}; bounds.serialized_bytes = 283;
    rejects([&] { odh::HitEntryCatalog invalid(order, bounds); }, "aggregate budget");
    bounds = {}; bounds.serialized_bytes = 284; bounds.track_data_rows = 4; bounds.groups = 2;
    require(odh::HitEntryCatalog(order, bounds).usage().serialized_bytes == 284,
            "exact aggregate budgets admit input");
    bounds.collision_records = 0;
    rejects([&] { odh::HitEntryCatalog invalid(order, bounds); }, "collision record budget");
    bounds = {}; bounds.event_name_bytes = 4;
    const odh::HitEntryCatalog short_names(order, bounds);
    rejects([&] { short_names.resolve_event(0, "alpha"); }, "over-budget event name");

    rejects([&] { catalog.resolve_event(2, "alpha"); }, "event group");
    rejects([&] { catalog.resolve_event(0, "alpha", true); }, "loop request");
    rejects([&] { catalog.resolve_event(0, "missing"); }, "event missing");
    {
        auto unsupported = first_hot;
        unsupported.events["alpha"].event_type = 2;
        const odh::HitEntryCatalog other({{&first_hit, &unsupported}});
        rejects([&] { other.resolve_event(0, "alpha"); }, "event mapping");
    }
    {
        auto unresolved = first_hot;
        unresolved.tracks[7].hitlist_id.reset();
        const odh::HitEntryCatalog other({{&first_hit, &unresolved}});
        rejects([&] { other.resolve_event(0, "alpha"); }, "entry key");
    }

    // Native parsing keeps CSV7 as an entry key without a local offset guess.
    // The key is registered by another group; ENTP cannot supply it.
    {
        odh::Hsm hsm;
        hsm.constants["entry"] = 100;
        const auto parsed = parsed_hot(
            "[EventMapping]\ncross=1,7\n"
            "[Track]\n7=0,label,1024,2,0,0,20,@entry,100\n", hsm);
        auto a = script({12}), b = script({12});
        auto other = hot(20);
        other.track_data[100] = 16;
        const odh::HitEntryCatalog parsed_catalog({{&a, &parsed}, {&b, &other}});
        require(parsed.tracks.at(7).unsupported_reason.empty() &&
                !parsed.tracks.at(7).subroutine_offset &&
                parsed_catalog.resolve_event(0, "cross").entry.file == &b,
                "global entry key resolves despite absent local TrackData offset");
    }

    // An admitted native controller starts in B using A's event/track metadata.
    // Calls B -> A -> C then returns C -> A -> B. All code is synthetic; the
    // callbacks only record requests at the portable audio boundary.
    {
        auto a = script({7,44,1,0,0, 8});   // Call key300, Return8.
        auto b = script({7,200,0,0,0, 12}); // Call key200, End12.
        auto c = script({8});               // Return8.
        odh::Hsm hsm;
        hsm.constants["entry"] = 100;
        auto ah = parsed_hot(
            "[TrackData]\n0xc8=0x11\n0xc8=0x10\n"
            "[EventMapping]\ncross=1,7\n"
            "[Track]\n7=0,label,1024,2,0,0,20,@entry,500\n"
            "[Patch]\n500=0,synthetic,\"synthetic.xa\",0,0\n", hsm);
        auto bh = hot(60), ch = hot(40);
        bh.track_data[100] = 16;
        ch.track_data[300] = 16;
        const odh::HitEntryCatalog calls({{&a, &ah}, {&b, &bh}, {&c, &ch}});
        const auto resolved = calls.resolve_event(0, "CROSS");
        require(resolved.entry.file == &b && resolved.entry.pc == 16 &&
                resolved.track == &ah.tracks.at(7) &&
                ah.track_data_replacements == 1 &&
                calls.usage().track_data_rows == 4 &&
                resolved.track->unsupported_reason.empty() &&
                !resolved.track->subroutine_offset,
                "native entry pairs target B with metadata from A");
        std::uint32_t bound_patch = 0;
        unsigned binds = 0;
        odh::NativeHitAudio audio{
            [&](std::uint32_t patch) { bound_patch = patch; ++binds; },
            [] { return false; },
            [](std::uint8_t) {},
            [] {}, [] {}, [] {}, [] {},
            [](std::uint8_t, std::int32_t) {}
        };
        odh::NativeHitController controller(
            *resolved.entry.file, ah, *resolved.track, resolved.entry.pc,
            odh::HitArguments{{1, 2, 3}}, 1024, 512, std::move(audio),
            {}, {}, {}, [&](std::uint32_t key) { return calls.lookup(key); });
        controller.activate(1);
        const auto result = controller.poll(1);
        require(result == odh::NativeHitPoll::stopped && binds == 1 &&
                bound_patch == 500 && controller.state().call_depth == 0,
                "cross-resource calls preserve original HOT patch and unwind frames");
        const auto& trace = controller.trace();
        require(trace.size() == 5 &&
                trace[0].source == &b && trace[0].next_source == &a &&
                trace[1].source == &a && trace[1].next_source == &c &&
                trace[2].source == &c && trace[2].next_source == &a &&
                trace[3].source == &a && trace[3].next_source == &b &&
                trace[4].source == &b && trace[4].next_source == &b,
                "Call7 and Return8 carry HIT resource identity through LIFO stack");
    }

    std::cout << checks << " HIT entry catalog checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
} }
