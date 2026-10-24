#include "odh/object_construction.hpp"

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

        const odh::SlotTable no_containment{0, 10, {slot(3), slot(2)}, 0};
        const auto no_slots = odh::object_construction_profile(definition(), no_containment);
        check(no_slots.status == odh::ObjectConstructionStatus::ready &&
                  no_slots.layout.containment_slots == 0,
              "objects without containment slots remain constructible");
        std::cout << checks << " object construction profile checks passed (synthetic fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "object construction profile test failed: " << error.what() << '\n';
        return 1;
    }
}
