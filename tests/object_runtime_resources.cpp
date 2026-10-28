#include "odh/create_object_instance.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const odh::FormatError&) { require(true, message); return; }
    require(false, message);
}
void put16(std::string& bytes, std::size_t offset, std::uint16_t value, bool big = false) {
    for (unsigned n = 0; n < 2; ++n)
        bytes.at(offset + n) = static_cast<char>((value >> (8 * (big ? 1 - n : n))) & 255);
}
void put32(std::string& bytes, std::size_t offset, std::uint32_t value, bool big = false) {
    for (unsigned n = 0; n < 4; ++n)
        bytes.at(offset + n) = static_cast<char>((value >> (8 * (big ? 3 - n : n))) & 255);
}
std::string chunk(const char* type, std::uint16_t id, const std::string& payload) {
    std::string result(76, '\0');
    result.replace(0, 4, type);
    put32(result, 4, static_cast<std::uint32_t>(76 + payload.size()), true);
    put16(result, 8, id, true);
    return result + payload;
}
std::string objd() {
    std::string result(162, '\0');
    put32(result, 0, 138);
    put16(result, 14, 137);
    put16(result, 18, 4);
    put32(result, 28, 0xf301e375);
    put16(result, 40, 128);
    put16(result, 44, 1); // UsesFnTable
    put16(result, 116, 16);
    return result;
}
std::string objf() {
    std::string result(24, '\0');
    put32(result, 4, 1);
    result.replace(8, 4, "fJBO");
    put32(result, 12, 2);
    put16(result, 18, 4100);
    put16(result, 22, 4177);
    return result;
}
std::string slot() {
    constexpr std::size_t span = 70;
    std::string result(16 + span * 3, '\0');
    put32(result, 4, 10);
    result.replace(8, 4, "TOLS");
    put32(result, 12, 3);
    put16(result, 16, 0);
    put16(result, 16 + span, 3);
    put16(result, 16 + 2 * span, 0);
    return result;
}
std::string bhav() {
    std::string result(24, '\0');
    put16(result, 0, 0x8002);
    put16(result, 2, 1);
    result[14] = static_cast<char>(0xfd);
    result[15] = static_cast<char>(0xfd);
    return result;
}
std::string iff(bool with_objf = true) {
    std::string result = "IFF FILE 2.5:TYPE FOLLOWED BY SIZE JAMIE DOORNBOS & MAXIS 1";
    result.resize(64, '\0');
    result += chunk("OBJD", 16808, objd());
    if (with_objf) result += chunk("OBJf", 16808, objf());
    result += chunk("SLOT", 128, slot());
    result += chunk("GLOB", 0, std::string(1, static_cast<char>(11)) + "FoodGlobals");
    result += chunk("BHAV", 4100, bhav());
    return result;
}
odh::ObjectRuntimeResources read(const std::string& bytes,
    odh::ObjectRuntimeResourceLimits limits = {}) {
    std::istringstream stream(bytes, std::ios::binary);
    const odh::View resource(stream, bytes.size());
    std::istringstream object(objd(), std::ios::binary);
    const auto definition = odh::read_object_definition(odh::View(object, 162));
    return odh::read_object_runtime_resources(resource,
        {"Objects.far", "Food.iff", 16808, definition}, limits);
}
}

int main() {
    try {
        const auto resources = read(iff());
        {
            const auto bytes=iff();std::istringstream stream(bytes,std::ios::binary);
            auto changed=resources.definition;changed.build_mode_type=7;
            rejects([&]{odh::read_object_runtime_resources(odh::View(stream,bytes.size()),
                {"Objects.far","Food.iff",16808,changed});},"changed BuildModeType rejects stale definition identity");
        }
        require(resources.definition.guid == 0xf301e375 &&
                    resources.definition.slot_table_id == 128,
                "definition identity and SlotID come from the selected OBJD");
        require(resources.construction.status == odh::ObjectConstructionStatus::ready &&
                    resources.construction.layout.attributes == 16 &&
                    resources.construction.layout.containment_slots == 2,
                "construction layout uses only the exact selected SLOT resource");
        require(resources.entry_points.source == odh::EntryPointSource::objf &&
                    resources.entry_points.entries.size() == 2 &&
                    resources.entry_points.entries[0].action == 4100 &&
                    resources.entry_points.entries[1].action == 4177,
                "OBJf entry points are resolved by the selected OBJD chunk ID");
        require(resources.private_routines.size() == 1 &&
                    resources.private_routines.count(odh::routine_key(4100)) == 1 &&
                    resources.semiglobal_name == std::optional<std::string>{"FoodGlobals"},
                "private BHAV and raw semiglobal link stay tied to one resource");
        odh::World world;
        require(world.create(1, odh::EntityKind::avatar) &&
                    odh::materialize_runtime_object(world, 4, resources) ==
                        odh::ObjectRuntimeCreateStatus::created,
                "resource-derived object is atomically materialized through World");
        const auto* entity = world.find(4);
        require(entity && entity->definition && entity->definition->guid == 0xf301e375 &&
                    entity->attributes.size() == 16 && entity->slots.size() == 2 &&
                    entity->entry_points && entity->entry_points->size() == 2,
                "materialized banks and entry points match the exact IFF resources");
        require(odh::materialize_runtime_object(world, 4, resources) ==
                    odh::ObjectRuntimeCreateStatus::world_rejected &&
                    world.snapshot().entities.size() == 2,
                "failed second materialization preserves the existing World");

        odh::World snack_world;
        require(snack_world.create(1, odh::EntityKind::object, {0, 0, 1, 0}) &&
                    snack_world.set_position(1, {80, 96, 1, 2}),
                "positioned fridge fixture is ready");
        const odh::CreateObjectInstanceRequest snack_request{
            0xf301e375, 4, 1, 0, 0};
        const auto snack = odh::create_object_instance_in_slot0(
            snack_world, 2, 1, snack_request, resources);
        const auto* fridge = snack_world.find(1);
        const auto* food = snack_world.find(2);
        require(snack.status == odh::CreateObjectInstanceStatus::created &&
                    snack.object_id == 2 && fridge && fridge->slots[0] == 2 && food &&
                    food->attachment && food->attachment->container == 1 &&
                    food->attachment->slot == 0 && food->attachment->positioned &&
                    food->position == std::optional<odh::EntityPosition>{{80, 96, 1, 2}},
                "Have A Snack creates Food and places it in fridge slot 0");
        odh::World out_of_world;
        const odh::CreateObjectInstanceRequest hidden_request{0xf301e375,6,0,0,0};
        const auto hidden=odh::create_object_instance_out_of_world(
            out_of_world,7,hidden_request,resources);
        const auto* hidden_food=out_of_world.find(7);
        require(hidden.status==odh::CreateObjectInstanceStatus::created&&hidden.object_id==7&&
                hidden_food&&!hidden_food->position&&!hidden_food->attachment,
                "OutOfWorld creation makes an unpositioned unattached runtime object");
        const auto occupied = odh::create_object_instance_in_slot0(
            snack_world, 3, 1, snack_request, resources);
        require(occupied.status == odh::CreateObjectInstanceStatus::slot_occupied &&
                    !occupied.object_id && snack_world.find(3) == nullptr,
                "occupied slot rejects a second Food without changing World");
        auto unsupported_request = snack_request;
        unsupported_request.raw_position = 0;
        require(odh::create_object_instance_in_slot0(
                    snack_world, 3, 1, unsupported_request, resources).status ==
                    odh::CreateObjectInstanceStatus::unsupported_position &&
                    snack_world.find(3) == nullptr,
                "other CreateObjectInstance positions fail without partial creation");

        auto bad = odh::ObjectRuntimeResourceLimits{};
        bad.private_routines = 0;
        rejects([&] { read(iff(), bad); }, "private BHAV count budget is enforced");
        bad = {};
        bad.private_instructions = 0;
        rejects([&] { read(iff(), bad); }, "private instruction budget is enforced");
        rejects([&] { read(iff(false)); }, "required OBJf table cannot be guessed");
        auto mismatch = iff();
        auto changed = objd();
        put32(changed, 28, 7);
        mismatch.replace(64 + 76, changed.size(), changed);
        rejects([&] { read(mismatch); }, "indexed GUID must match selected OBJD bytes");
        auto duplicate = iff();
        duplicate += chunk("SLOT", 128, slot());
        rejects([&] { read(duplicate); }, "duplicate selected SLOT fails closed");
        duplicate = iff();
        duplicate += chunk("BHAV", 4100, bhav());
        rejects([&] { read(duplicate); }, "duplicate private BHAV ID fails closed");
        duplicate = iff();
        duplicate += chunk("GLOB", 0, std::string(1, static_cast<char>(11)) + "FoodGlobals");
        rejects([&] { read(duplicate); }, "ambiguous semiglobal links fail closed");

        std::cout << checks << " object runtime resource checks passed (synthetic IFF fixtures only)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "object runtime resource test failed: " << error.what() << '\n';
        return 1;
    }
}
