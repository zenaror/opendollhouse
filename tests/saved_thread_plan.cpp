#include "odh/saved_thread_plan.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        odh::SavedWorldEntity entity{};
        entity.saved.object_id = 105;
        entity.saved.prefix.temps = {1,2,3,4,5,6,7,8};
        odh::ObjmStackFrame base{};
        base.stack_object_id = 12; base.tree_id = 4096; base.node_id = 9;
        base.parameters = {31}; base.locals = {41,42}; base.primitive_state = 777;
        base.code_owner_obj_type = 1;
        odh::ObjmStackFrame child{};
        child.stack_object_id = 13; child.tree_id = 8192; child.node_id = 2;
        child.code_owner_obj_type = 2;
        entity.common.stack = {base, child};
        entity.tail.person.emplace();
        entity.tail.person->object_uses.push_back({13, 2, 0});

        odh::Objt types{};
        types.entries.resize(2);
        types.entries[0].guid = 0x12345678;
        types.entries[1].guid = 0x87654321;
        const auto plan = odh::make_simitone_saved_thread_plan(entity, types);
        require(plan.entity_id == 105 && plan.frames.size() == 2,
                "saved actor and both frames retained");
        require(plan.temp_registers[0] == 1 && plan.temp_registers[7] == 8 &&
                plan.temp_registers[8] == 0 && plan.temp_registers[19] == 0,
                "eight saved temps padded to twenty");
        require(plan.frames[0].routine_id == 4096 && plan.frames[0].instruction_pointer == 9 &&
                plan.frames[0].code_owner_guid == 0x12345678,
                "first frame routine, node, and owner resolved");
        require(plan.frames[0].arguments == std::vector<std::int16_t>({31,0,0,0}) &&
                plan.frames[0].locals == std::vector<std::int16_t>({41,42}),
                "arguments padded and locals preserved");
        require(plan.frames[1].code_owner_guid == 0x87654321 &&
                plan.frames[0].callee == 105 && plan.frames[1].callee == 13,
                "ObjectUses callee mapping applies from saved stack depth");
        require(plan.frames[0].saved_primitive_state == 777,
                "uninterpreted primitive state remains visible");

        auto rejects = [&](auto mutate, const char* message) {
            auto bad = entity; mutate(bad);
            try { (void)odh::make_simitone_saved_thread_plan(bad, types); }
            catch (const odh::FormatError&) { return; }
            throw std::runtime_error(message);
        };
        rejects([](auto& e) { e.common.stack[0].code_owner_obj_type = 0; },
                "zero OBJT owner rejected");
        rejects([](auto& e) { e.saved.object_id = 65535; },
                "out-of-range actor ID rejected");
        rejects([](auto& e) { e.common.stack[0].code_owner_obj_type = 3; },
                "out-of-range OBJT owner rejected");
        rejects([](auto& e) { e.tail.person->object_uses[0].stack_length = 3; },
                "ObjectUses frame beyond saved stack rejected");

        types.entries[0].guid = 0;
        rejects([](auto&) {}, "zero GUID code owner rejected");
        std::cout << "saved Simitone thread plan checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "saved thread plan test failed: " << e.what() << '\n';
        return 1;
    }
}
