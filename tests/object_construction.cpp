#include "odh/object_construction.hpp"
#include "odh/world.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
odh::SlotRecord slot(std::uint16_t type) {
    odh::SlotRecord result{};
    result.type = type;
    return result;
}
odh::ObjectDefinition definition() {
    odh::ObjectDefinition result{};
    result.version = 138;
    result.guid = 0xf301e375;
    result.slot_table_id = 128;
    result.num_attributes = 16;
    return result;
}
}

int main() {
    try {
        unsigned checks = 0;
        auto check = [&](bool condition, const char* message) {
            ++checks;
            require(condition, message);
        };

        const odh::SlotTable slots{0, 10,
            {slot(3), slot(0), slot(2), slot(0), slot(3)}, 0};
        const auto profile = odh::object_construction_profile(definition(), slots);
        check(profile.status == odh::ObjectConstructionStatus::ready,
              "complete ordinary object profile is ready");
        check(profile.layout.attributes == 16 && profile.layout.containment_slots == 2,
              "attribute bank and type-0 containment slots are counted");

        auto d = definition();
        d.is_multitile = true;
        check(odh::object_construction_profile(d, slots).status ==
                  odh::ObjectConstructionStatus::unsupported_multitile,
              "multitile definitions are rejected explicitly");
        d.master_id=42;d.sub_index=0x0102;
        const auto part_profile=odh::object_construction_profile(d,slots,{},true);
        check(part_profile.status==odh::ObjectConstructionStatus::ready&&
            part_profile.layout.attributes==16&&part_profile.layout.containment_slots==2,
            "explicit selected subpart allocates only its own attribute and SLOT banks");
        check(odh::object_construction_profile(d,slots).status==odh::ObjectConstructionStatus::unsupported_multitile,
            "selected-subpart opt-in never changes the default rejection profile");
        d.sub_index=-1;
        check(odh::object_construction_profile(d,slots,{},true).status==odh::ObjectConstructionStatus::unsupported_multitile,
            "abstract master remains non-materializable even with subpart opt-in");
        d.sub_index=0;d.num_attributes.reset();
        check(odh::object_construction_profile(d,slots,{},true).status==odh::ObjectConstructionStatus::missing_runtime_fields,
            "subpart opt-in cannot invent missing bank metadata");
        d = definition();
        d.slot_table_id.reset();
        check(odh::object_construction_profile(d, slots).status ==
                  odh::ObjectConstructionStatus::missing_runtime_fields,
              "missing slot table identity is not guessed");
        d = definition();
        d.num_attributes.reset();
        check(odh::object_construction_profile(d, slots).status ==
                  odh::ObjectConstructionStatus::missing_runtime_fields,
              "missing attribute count is not guessed");

        d = definition();
        d.num_attributes = 5;
        auto limits = odh::ObjectConstructionLimits{4, 8};
        check(odh::object_construction_profile(d, slots, limits).status ==
                  odh::ObjectConstructionStatus::resource_limit_exceeded,
              "attribute budget is enforced");
        limits = {32, 1};
        check(odh::object_construction_profile(definition(), slots, limits).status ==
                  odh::ObjectConstructionStatus::resource_limit_exceeded,
              "containment slot budget is enforced");
        auto bounded_part=definition();bounded_part.is_multitile=true;bounded_part.master_id=42;bounded_part.sub_index=1;
        check(odh::object_construction_profile(bounded_part,slots,{4,8},true).status==
            odh::ObjectConstructionStatus::resource_limit_exceeded,
            "selected subpart retains normal allocation budgets before any body is created");

        const odh::SlotTable no_containment{0, 10, {slot(3), slot(2)}, 0};
        const auto no_slots = odh::object_construction_profile(definition(), no_containment);
        check(no_slots.status == odh::ObjectConstructionStatus::ready &&
                  no_slots.layout.containment_slots == 0,
              "objects without containment slots remain constructible");

        odh::World world;
        check(world.create(1, odh::EntityKind::avatar),
              "existing World accepts an empty avatar before object construction");
        const auto before = world.snapshot();
        check(world.create_runtime_object(4, 0xf301e375,
                  {profile.layout.attributes, 0, profile.layout.containment_slots, 0},
                  {{0, 4100}, {0, 4177}}),
              "runtime object publishes with its definition and entry points");
        const auto* food = world.find(4);
        check(food && food->definition && food->definition->guid == 0xf301e375 &&
                  food->attributes.size() == 16 && food->slots.size() == 2 &&
                  food->object_words.size() == 75 && food->entry_points &&
                  food->entry_points->size() == 2 && world.word_count() == 97,
              "runtime banks are initialized and owned by the World");
        const odh::VmFrame object_frame{odh::routine_key(4100), 0, {1, 4, 4}, {}, {}};
        check(world.read(object_frame, 4, 0) == std::optional<std::int16_t>{0} &&
                  world.read(object_frame, 16, 0) == std::optional<std::int16_t>{0} &&
                  world.read(object_frame, 35, 0) == std::optional<std::int16_t>{4100},
              "new object exposes zeroed attributes/slots and its copied entry point");
        const auto live = world.snapshot();
        check(!world.create_runtime_object(4, 0xf301e375, {}, {}) &&
                  world.snapshot().entities.size() == live.entities.size() &&
                  world.snapshot().word_count == live.word_count,
              "duplicate identity fails without publishing partial state");
        for(const auto invalid_master:{0U,0xf301e375U}) {
            const auto unchanged=world.snapshot();
            check(!world.create_runtime_object(5,0xf301e375,{}, {},invalid_master)&&
                world.snapshot().entities.size()==unchanged.entities.size()&&
                world.snapshot().generation_sequence==unchanged.generation_sequence&&
                world.snapshot().spatial_revision==unchanged.spatial_revision&&world.word_count()==unchanged.word_count,
                "zero or self master override is rejected before changing World state");
        }
        check(world.create_runtime_object(5,0xf301e375,{}, {},0x81234567U)&&
            world.find(5)->definition->guid==0xf301e375U&&world.find(5)->definition->master_guid==0x81234567U,
            "explicit group master remains separate from actual part definition identity");
        odh::World tight({2, 32, 128, 16, 16});
        check(tight.create(1, odh::EntityKind::avatar),
              "tight World fixture starts with one existing entity");
        const auto tight_before = tight.snapshot();
        check(!tight.create_runtime_object(4, 0xf301e375,
                  {profile.layout.attributes, 0, profile.layout.containment_slots, 0},
                  {{0, 4100}, {0, 4177}}) &&
                  tight.snapshot().entities.size() == 1 &&
                  tight.snapshot().generation_sequence == tight_before.generation_sequence &&
                  tight.snapshot().word_count == tight_before.word_count &&
                  tight.snapshot().spatial_revision == tight_before.spatial_revision,
              "word budget failure leaves the World unchanged");
        std::cout << checks << " object construction profile checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "object construction profile test failed: " << error.what() << '\n';
        return 1;
    }
}
