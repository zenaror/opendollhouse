#include "odh/hit_resource_bank.hpp"
#include "odh/hit_native_controller.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejects(F action, const std::string& reason) {
    bool rejected = false;
    try { action(); } catch (const odh::FormatError& error) {
        rejected = std::string(error.what()).find(reason) != std::string::npos;
    }
    require(rejected, "bank must reject over-budget or invalid input");
}
void word(std::string& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = static_cast<char>((value >> (8 * i)) & 255);
}
std::string binary(bool caller) {
    std::string bytes(32, '\0');
    bytes.replace(0, 4, "HIT!"); word(bytes, 4, 1); word(bytes, 8, 8);
    bytes.replace(12, 4, "TRAX");
    if (caller) {
        bytes[16] = 7; word(bytes, 17, 200); // Call7 into group 1.
        bytes[21] = 12; bytes[22] = 8;
    } else {
        bytes[16] = 8; bytes[17] = 12; // Return8 then End12.
    }
    return bytes;
}
const std::string& first_hsm() {
    static const std::string value = "Foo 100\nfoo 300\nShared 400\n";
    return value;
}
const std::string& second_hsm() {
    static const std::string value = "Foo 200\nOther 500\n";
    return value;
}
const std::string& first_hot() {
    static const std::string value =
        "[TrackData]\n100=16\n300=21\n400=22\n"
        "[EventMapping]\nold=1,7\n"
        "[Track]\n7=0,old,1024,2,0,0,20,@Foo,500\n"
        "[Patch]\n500=0,synthetic,\"synthetic.xa\",0,0\n";
    return value;
}
const std::string& second_hot() {
    static const std::string value =
        "[TrackData]\n200=16\n500=17\n"
        "[EventMapping]\nnew=1,9\ncross=1,10\nshared=1,11\n"
        "[Track]\n9=0,new,1024,2,0,0,20,@Foo,0\n"
        "10=0,cross,1024,2,0,0,20,@foo,0\n"
        "11=0,shared,1024,2,0,0,20,@Shared,0\n"
        "7=0,replacement,1024,2,0,0,20,@Foo,500\n";
    return value;
}
std::unique_ptr<odh::HitResourceBank> build(odh::HitResourceBankLimits limits = {}) {
    // All streams die on return; the bank must keep independent owned data.
    const auto first_binary = binary(true), second_binary = binary(false);
    std::istringstream a_hsm(first_hsm()), a_hit(first_binary), a_hot(first_hot());
    std::istringstream b_hsm(second_hsm()), b_hit(second_binary), b_hot(second_hot());
    const std::vector<odh::HitResourceGroupInput> input{
        {odh::View(a_hsm, first_hsm().size()), odh::View(a_hit, first_binary.size()),
         odh::View(a_hot, first_hot().size())},
        {odh::View(b_hsm, second_hsm().size()), odh::View(b_hit, second_binary.size()),
         odh::View(b_hot, second_hot().size())}
    };
    return std::make_unique<odh::HitResourceBank>(input, limits);
}
}

static_assert(!std::is_copy_constructible_v<odh::HitResourceBank>);
static_assert(!std::is_move_constructible_v<odh::HitResourceBank>);

int main() { try {
    auto bank = build();
    const auto source_bytes = first_hsm().size() + second_hsm().size() +
        2 * binary(true).size() + first_hot().size() + second_hot().size();
    require(bank->usage().groups == 2 && bank->usage().source_bytes == source_bytes &&
            bank->usage().symbol_records == 5 && bank->usage().symbol_entries == 4 &&
            bank->usage().symbol_replacements == 1 &&
            bank->usage().symbol_name_bytes == 17,
            "bank counts declared source extents and unique symbol names");
    const auto& symbols = bank->symbols();
    require(symbols.logical_bytes == first_hsm().size() + second_hsm().size() &&
            symbols.records == 5 && symbols.duplicate_names == 1 &&
            symbols.constants.at("Foo") == 200 && symbols.constants.at("foo") == 300 &&
            symbols.constants.at("Shared") == 400 &&
            symbols.first_lines.at("Foo") == 1 && symbols.first_lines.at("Other") == 2,
            "global HSM namespace is case-sensitive last-wins with first local line");
    require(bank->hot(0).tracks.at(7).hitlist_id == 100 &&
            bank->hot(1).tracks.at(9).hitlist_id == 200 &&
            bank->hot(1).tracks.at(10).hitlist_id == 300 &&
            bank->hot(1).tracks.at(11).hitlist_id == 400,
            "each HOT retains numeric resolution from its HSM snapshot");
    require(bank->entries().lookup(100)->file == &bank->hit(0) &&
            bank->entries().lookup(200)->file == &bank->hit(1) &&
            bank->entries().resolve_event(1, "cross").entry.file == &bank->hit(0) &&
            bank->entries().resolve_event(1, "shared").entry.pc == 22,
            "entry catalog resolves keys across owned HIT groups after streams die");
    const auto global_old = bank->objects().resolve_global_track_event(0, "old");
    require(global_old.event == &bank->hot(0).events.at("old") &&
            global_old.track.object.group == 1 &&
            global_old.track.object.track == &bank->hot(1).tracks.at(7) &&
            global_old.track.entry_key == 200 &&
            global_old.track.entry.file == &bank->hit(1) &&
            bank->objects().lookup_patch(500)->patch == &bank->hot(0).patches.at(500) &&
            bank->objects().usage().object_records == 6 &&
            bank->objects().usage().collision_records == 1,
            "owned global objects resolve event, replacement Track and Patch from distinct groups");

    const auto old = bank->entries().resolve_event(0, "old");
    unsigned binds = 0;
    odh::NativeHitAudio audio{
        [&](std::uint32_t) { ++binds; }, [] { return false; },
        [](std::uint8_t) {}, [] {}, [] {}, [] {}, [] {},
        [](std::uint8_t, std::int32_t) {}
    };
    odh::NativeHitController controller(
        *old.entry.file, bank->hot(0), *old.track, old.entry.pc,
        odh::HitArguments{{1, 2, 3}}, 1024, 512, std::move(audio),
        {}, {}, {}, [&](std::uint32_t key) { return bank->entries().lookup(key); });
    controller.activate(1);
    require(controller.poll(1) == odh::NativeHitPoll::stopped && binds == 1 &&
            controller.trace().size() == 3 &&
            controller.trace()[0].source == &bank->hit(0) &&
            controller.trace()[0].next_source == &bank->hit(1) &&
            controller.trace()[1].source == &bank->hit(1) &&
            controller.trace()[1].next_source == &bank->hit(0),
            "Call7 and Return8 execute across bank-owned resources");

    auto limits = odh::HitResourceBankLimits{};
    limits.source_bytes = source_bytes - 1;
    rejects([&] { build(limits); }, "aggregate source budget");
    limits = {}; limits.per_file.hit_bytes = 31;
    rejects([&] { build(limits); }, "per-file source budget");
    limits = {}; limits.catalog.groups = 1;
    rejects([&] { build(limits); }, "group or bank budget");
    limits = {};
    limits.catalog.serialized_bytes = 2 * binary(true).size() +
        first_hot().size() + second_hot().size() - 1;
    rejects([&] { build(limits); }, "catalog serialized budget");
    limits = {}; limits.symbol_records = 4;
    rejects([&] { build(limits); }, "symbol record budget");
    limits = {}; limits.symbol_entries = 3;
    rejects([&] { build(limits); }, "symbol entry budget");
    limits = {}; limits.symbol_name_bytes = 16;
    rejects([&] { build(limits); }, "symbol name byte budget");
    limits = {}; limits.catalog.track_data_rows = 4;
    rejects([&] { build(limits); }, "aggregate budget");
    limits = {}; limits.objects.object_records = 5;
    rejects([&] { build(limits); }, "object record budget");
    limits = {}; limits.objects.groups = 1;
    rejects([&] { build(limits); }, "group or bank budget");
    rejects([&] { bank->hit(2); }, "group index");
    rejects([&] { bank->hot(2); }, "group index");

    // Three declared Views fit streamoff individually but overflow uint64
    // in aggregate. Preflight must reject without reading or allocating them.
    std::istringstream empty;
    const auto huge = static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max());
    const std::vector<odh::HitResourceGroupInput> declared{
        {odh::View(empty, huge), odh::View(empty, huge), odh::View(empty, huge)}
    };
    limits = {};
    limits.per_file.input_bytes = huge;
    limits.per_file.text_bytes = huge;
    limits.per_file.hit_bytes = huge;
    limits.source_bytes = std::numeric_limits<std::uint64_t>::max();
    rejects([&] { odh::HitResourceBank rejected(declared, limits); },
            "aggregate source budget");

    std::cout << checks << " HIT resource bank checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
} }
