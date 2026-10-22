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
        require(plan.object_uses.size() == 1 &&
                plan.object_uses[0].target_id == 13 && plan.object_uses[0].stack_length == 2 &&
                plan.frames[0].return_state == odh::SimitoneSavedReturnState::unresolved,
                "object-use depth is retained and return state stays unresolved without code lookup");

        odh::RoutineSet private_code{{odh::routine_key(4096),
            odh::Bhav{0x8002,std::vector<odh::Instruction>(10,odh::Instruction{2,0xFE,0xFF,{}}),{},
                odh::Bhav::Metadata{0,4,2,0}}}};
        odh::RoutineSet semiglobal_code{{odh::routine_key(8192),
            odh::Bhav{0x8002,std::vector<odh::Instruction>(10,odh::Instruction{2,0xFE,0xFF,{}}),{},
                odh::Bhav::Metadata{0,4,2,0}}}};
        odh::RoutineOwners owner_code{{0x12345678,std::move(private_code)},
                                       {0x87654321,std::move(semiglobal_code)}};
        odh::Vm restored_vm({}, {}, {}, {}, std::move(owner_code));
        auto normal_plan=plan;
        for (auto& frame : normal_plan.frames)
            frame.return_state=odh::SimitoneSavedReturnState::normal;
        odh::Vm no_code_vm(odh::RoutineSet{}, {}, {}, {}, {});
        bool unavailable_code_rejected=false;
        try { odh::restore_simitone_saved_thread_plan(no_code_vm,normal_plan); }
        catch (const std::invalid_argument&) { unavailable_code_rejected=true; }
        require(unavailable_code_rejected && no_code_vm.status()==odh::VmStatus::ready,
                "VM rejects saved frames whose routines are not installed");
        odh::restore_simitone_saved_thread_plan(restored_vm,normal_plan);
        require(restored_vm.status()==odh::VmStatus::dormant && restored_vm.stack().size()==2 &&
                restored_vm.stack()[0].key.id==4096 && restored_vm.stack()[0].pc==9 &&
                restored_vm.stack()[0].context.code_owner==0x12345678 &&
                restored_vm.stack()[1].key.id==8192 && restored_vm.stack()[1].pc==2 &&
                restored_vm.stack()[1].context.callee==13 &&
                restored_vm.temps()==normal_plan.temp_registers,
                "normal-return saved plan imports owner-resolved frames and temps into dormant VM");
        auto special_plan=normal_plan;
        special_plan.frames[1].return_state=odh::SimitoneSavedReturnState::retry;
        bool special_state_rejected=false;
        try { odh::restore_simitone_saved_thread_plan(restored_vm,special_plan); }
        catch (const odh::FormatError&) { special_state_rejected=true; }
        require(special_state_rejected && restored_vm.status()==odh::VmStatus::dormant &&
                restored_vm.stack()[1].pc==2,
                "unsupported return state is rejected without changing restored VM");

        auto missing_opcode = plan;
        try {
            odh::apply_simitone_saved_return_mapping(missing_opcode, {});
            throw std::runtime_error("missing saved caller opcode should fail");
        } catch (const odh::FormatError&) {}

        auto map_opcode = [](std::uint16_t opcode) {
            return [opcode](const odh::SimitoneSavedFramePlan&) {
                return std::optional<std::uint16_t>{opcode};
            };
        };
        for (const auto opcode : {std::uint16_t{27}, std::uint16_t{45}}) {
            auto retry = plan;
            odh::apply_simitone_saved_return_mapping(retry, map_opcode(opcode));
            require(retry.frames[1].return_state == odh::SimitoneSavedReturnState::retry &&
                    !retry.frames[1].action_tree,
                    "GotoRelative and GotoSlot restore Retry return state");
        }
        for (const auto opcode : {std::uint16_t{30}, std::uint16_t{17}}) {
            auto interaction = plan;
            interaction.frames.push_back(plan.frames[1]);
            odh::apply_simitone_saved_return_mapping(interaction, map_opcode(opcode));
            require(interaction.frames[1].return_state == odh::SimitoneSavedReturnState::interaction &&
                    interaction.frames[1].action_tree && interaction.frames[2].action_tree &&
                    interaction.frames[2].callee == 13,
                    "Gosub and IdleForInput restore interaction state through the top frame");
        }
        auto ordinary = plan;
        odh::apply_simitone_saved_return_mapping(ordinary, map_opcode(2));
        require(ordinary.frames[1].return_state == odh::SimitoneSavedReturnState::normal &&
                !ordinary.frames[1].action_tree,
                "other caller instructions restore the normal return state");
        auto absent_opcode = plan;
        try {
            odh::apply_simitone_saved_return_mapping(absent_opcode,
                [](const odh::SimitoneSavedFramePlan&) -> std::optional<std::uint16_t> { return {}; });
            throw std::runtime_error("missing caller instruction should fail");
        } catch (const odh::FormatError&) {}

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
