#include "odh/household_spawn.hpp"
#include <iostream>

namespace {
unsigned checks = 0;
void check_impl(bool value, int line) {
    ++checks;
    if (!value) throw std::runtime_error("household spawn assertion at line " + std::to_string(line));
}
#define check(v) check_impl((v), __LINE__)
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { check(true); return; }
    check(false);
}
odh::NeighborRecord neighbor(std::uint32_t guid, std::int16_t id = 42) {
    odh::NeighborRecord result{};
    result.version = 4; result.person_mode = 1; result.neighbor_id = id; result.guid = guid;
    result.person_data = std::vector<std::int16_t>(80, 17);
    (*result.person_data)[61] = 123;
    return result;
}
odh::WorldSnapshot source_world() {
    odh::World source;
    check(source.create(1, odh::EntityKind::object));
    check(source.define_type(1, 0x1001));
    check(source.create(5, odh::EntityKind::avatar, {0, 101, 3, 16}));
    check(source.define_type(5, 0xaaaa));
    check(source.set_position(5, {32, 48, 1, 2}));
    check(source.declare_object_word(5, 8, 333));
    check(source.create(7, odh::EntityKind::avatar, {0, 101, 3, 16}));
    check(source.define_type(7, 0xcccc));
    return source.snapshot();
}
}
int main() {
    try {
        constexpr std::uint32_t old_guid = 0xaaaa, absent_guid = 0xbbbb;
        auto source = source_world();
        const auto original = source;
        const auto nb = neighbor(absent_guid);
        bool staged_prepared = false;
        auto staged = odh::stage_household_members(source,
            {{old_guid, nullptr}, {absent_guid, &nb}}, 123,
            [&](odh::World& world, std::int16_t id, std::uint32_t guid) {
                staged_prepared = true;
                const auto* avatar = world.find(id);
                check(guid == absent_guid && avatar && avatar->definition->guid == guid);
                check(avatar->person_data[31] == 0 && avatar->person_data[61] == 0);
                return world.set_person_data(id, 65, 1);
            });
        check(staged_prepared && staged.runtime_ids.at(old_guid) == 5 &&
              staged.runtime_ids.at(absent_guid) == 2);
        check(staged.new_members.size() == 1 && staged.new_members[0].runtime_id == 2);
        const auto* staged_avatar = staged.candidate->find(2);
        check(staged_avatar && !staged_avatar->position && staged_avatar->person_data[31] == 0 &&
              staged_avatar->person_data[61] == 0 && staged_avatar->person_data[65] == 1);
        odh::inherit_staged_household_member(*staged.candidate,
            staged.runtime_ids.at(absent_guid), nb, 123);
        staged_avatar = staged.candidate->find(staged.runtime_ids.at(absent_guid));
        check(staged_avatar && !staged_avatar->position && staged_avatar->person_data[31] == 42 &&
              staged_avatar->person_data[61] == 123 && staged_avatar->person_data[65] == 1);
        check(source.entities.size() == original.entities.size() &&
              source.entities[1].object_words == original.entities[1].object_words);

        bool prepared = false, resolved = false;
        auto result = odh::materialize_household_members(source,
            {{old_guid, nullptr}, {absent_guid, &nb}}, 123,
            [&](odh::World& world, std::int16_t id, std::uint32_t guid) {
                prepared = true;
                const auto* avatar = world.find(id);
                check(guid == absent_guid && avatar && avatar->definition->guid == guid);
                check(avatar->person_data[31] == 0 && avatar->person_data[61] == 0);
                return world.set_person_data(id, 65, 1);
            },
            [&](const odh::World& world, std::int16_t id, std::uint32_t) -> std::optional<odh::EntityPosition> {
                resolved = true;
                check(prepared && world.find(id)->person_data[61] == 123);
                return odh::EntityPosition{160, 224, 1, 4};
            });
        check(source.entities.size() == original.entities.size());
        check(source.entities[1].object_words == original.entities[1].object_words);
        check(result.runtime_ids.size() == 2 && result.runtime_ids.at(old_guid) == 5 &&
              result.runtime_ids.at(absent_guid) == 2 && !result.runtime_ids.count(0xcccc));
        check(result.new_members.size() == 1 && result.new_members[0].runtime_id == 2);
        check(prepared && resolved);
        const auto* existing = result.candidate->find(5);
        check(existing && existing->position == original.entities[1].position && existing->object_words.at(8) == 333);
        const auto* added = result.candidate->find(2);
        check(added && added->kind == odh::EntityKind::avatar && added->definition->guid == absent_guid);
        check(added->person_data.size() == 88 && added->person_data[31] == 42);
        check(added->person_data[61] == 123 && added->person_data[65] == 1 &&
              added->person_data[32] == 0);
        check((added->position == std::optional<odh::EntityPosition>{{160, 224, 1, 4}}));
        check(added->object_words.at(8) == 256 && added->object_words.at(0) == 0);
        check(!added->saved_object_banks);

        rejects([&] { odh::stage_household_members(source, {{absent_guid, &nb}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return false; }); });
        check(source.entities.size() == original.entities.size() &&
              source.entities[1].person_data == original.entities[1].person_data);

        auto second = odh::materialize_household_members(source, {{absent_guid, &nb}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) {
                return std::optional<odh::EntityPosition>{{160, 224, 1, 4}};
            });
        check(second.runtime_ids.size() == 1 && second.runtime_ids.at(absent_guid) == 2);

        constexpr std::uint32_t sibling_guid = 0xdddd;
        const auto sibling_neighbor = neighbor(sibling_guid, 43);
        bool sibling_saw_first_arrival = false;
        const auto sequential = odh::materialize_household_members(source,
            {{absent_guid, &nb}, {sibling_guid, &sibling_neighbor}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [&](const odh::World& world, std::int16_t id, std::uint32_t guid)
                -> std::optional<odh::EntityPosition> {
                if (guid == sibling_guid) {
                    const auto* first = world.find(2);
                    sibling_saw_first_arrival = id == 3 && first &&
                        first->position == std::optional<odh::EntityPosition>{{160, 224, 1, 4}};
                    return odh::EntityPosition{176, 224, 1, 4};
                }
                return odh::EntityPosition{160, 224, 1, 4};
            });
        const std::optional<odh::EntityPosition> expected_sibling_position{{176, 224, 1, 4}};
        check(sibling_saw_first_arrival && sequential.runtime_ids.at(sibling_guid) == 3 &&
              sequential.candidate->find(3)->position == expected_sibling_position);

        rejects([&] { odh::materialize_household_members(source,
            {{absent_guid, &nb}, {absent_guid, &nb}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; }); });
        rejects([&] { odh::materialize_household_members(source,
            {{0, &nb}}, 123, [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; }); });
        rejects([&] { odh::materialize_household_members(source,
            {{absent_guid, nullptr}}, 123, [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; }); });
        auto mismatched = neighbor(0xeeee);
        rejects([&] { odh::materialize_household_members(source,
            {{absent_guid, &mismatched}}, 123, [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; }); });

        for (int fail_at = 0; fail_at < 4; ++fail_at) {
            const auto before = source;
            rejects([&] { odh::materialize_household_members(source, {{absent_guid, &nb}}, 123,
                [fail_at](odh::World&, std::int16_t, std::uint32_t) {
                    if (fail_at == 0) return false;
                    if (fail_at == 1) throw std::runtime_error("prep exception");
                    return true;
                },
                [fail_at](const odh::World&, std::int16_t, std::uint32_t) -> std::optional<odh::EntityPosition> {
                    if (fail_at == 2) return std::nullopt;
                    if (fail_at == 3) throw std::runtime_error("arrival exception");
                    return odh::EntityPosition{1, 1, 1, 0};
                }); });
            check(source.entities.size() == before.entities.size() && source.entities[1].person_data == before.entities[1].person_data);
        }
        auto bad_neighbor = neighbor(absent_guid);
        bad_neighbor.version = 3;
        const auto before_bad_neighbor = source;
        rejects([&] { odh::materialize_household_members(source, {{absent_guid, &bad_neighbor}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; }); });
        check(source.entities.size() == before_bad_neighbor.entities.size() &&
              source.word_count == before_bad_neighbor.word_count);
        auto high_chunk = odh::materialize_household_members(source, {{absent_guid, &nb}}, 65535,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; });
        check(high_chunk.candidate->find(high_chunk.runtime_ids.at(absent_guid))->person_data[61] == -1);
        auto unknown_avatar = source;
        unknown_avatar.entities[1].definition.reset();
        rejects([&] { odh::materialize_household_members(unknown_avatar, {{absent_guid, &nb}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; }); });
        odh::WorldSnapshot duplicates = source;
        auto duplicate_avatar = duplicates.entities[1]; duplicate_avatar.id = 6; duplicate_avatar.generation = 4;
        duplicates.entities.push_back(duplicate_avatar); duplicates.generation_sequence = 4;
        duplicates.word_count += duplicate_avatar.person_data.size() + duplicate_avatar.slots.size() +
            duplicate_avatar.motives.size() + duplicate_avatar.object_words.size();
        rejects([&] { odh::materialize_household_members(duplicates, {{absent_guid, &nb}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; }); });
        rejects([&] { odh::materialize_household_members(source, {{absent_guid, &nb}}, 123,
            [](odh::World&, std::int16_t, std::uint32_t) { return true; },
            [](const odh::World&, std::int16_t, std::uint32_t) { return std::optional<odh::EntityPosition>{{1,1,1,0}}; },
            {3, 200, 1000}); });
        std::cout << "household spawn checks: " << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
