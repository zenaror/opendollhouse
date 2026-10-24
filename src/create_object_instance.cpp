#include "odh/create_object_instance.hpp"

namespace odh {

CreateObjectInstanceRequest decode_create_object_instance(const Instruction& instruction) {
    if (instruction.opcode != 42)
        throw FormatError("CreateObjectInstance decoder requires opcode 42");
    const auto& bytes = instruction.operands;
    const auto guid = static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
    return {guid, bytes[4], bytes[5], bytes[6], bytes[7]};
}

} // namespace odh
