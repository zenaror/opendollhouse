#include "odh/create_object_instance.hpp"

#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        odh::Instruction fridge{};
        fridge.opcode = 42;
        fridge.operands = {117, 227, 1, 243, 4, 1, 0, 0};
        const auto food = odh::decode_create_object_instance(fridge);
        require(food.guid == 0xf301e375 && food.has_known_position() &&
                food.position() == odh::CreateObjectPosition::in_slot0_of_stack_object,
                "Have A Snack decodes the Food GUID and slot-0 position");
        require(food.flags == 1 && food.no_duplicate() && !food.pass_object_ids() &&
                !food.use_neighbor() && !food.fail_if_non_empty() && !food.pass_temp0() &&
                !food.face_stack_object_direction() && !food.return_immediately() &&
                !food.persist_in_database() && food.local_to_use == 0 &&
                food.interaction_callback == 0,
                "Have A Snack decodes only the NoDuplicate option");

        fridge.operands[5] = 0xff;
        const auto all_flags = odh::decode_create_object_instance(fridge);
        require(all_flags.no_duplicate() && all_flags.pass_object_ids() &&
                all_flags.use_neighbor() && all_flags.fail_if_non_empty() &&
                all_flags.pass_temp0() && all_flags.face_stack_object_direction() &&
                all_flags.return_immediately() && all_flags.persist_in_database(),
                "all eight option bits have distinct meanings");

        fridge.operands[4] = 255;
        const auto unknown_position = odh::decode_create_object_instance(fridge);
        require(!unknown_position.has_known_position() && unknown_position.raw_position == 255,
                "unknown position values are retained for an explicit unsupported result");

        fridge.opcode = 41;
        bool rejected = false;
        try { (void)odh::decode_create_object_instance(fridge); }
        catch (const odh::FormatError&) { rejected = true; }
        require(rejected, "other opcodes cannot be decoded as CreateObjectInstance");
        std::cout << checks << " CreateObjectInstance operand checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CreateObjectInstance test failed: " << error.what() << '\n';
        return 1;
    }
}
